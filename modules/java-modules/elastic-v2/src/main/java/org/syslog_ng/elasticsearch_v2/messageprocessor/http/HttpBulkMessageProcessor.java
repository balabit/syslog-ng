/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Laszlo Budai <laszlo.budai@balabit.com>
 * Copyright (c) 2016 Viktor Tusa <tusavik@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

package org.syslog_ng.elasticsearch_v2.messageprocessor.http;

import io.searchbox.client.JestResultHandler;
import io.searchbox.client.http.ESJestHttpClient;
import io.searchbox.core.Bulk;
import io.searchbox.core.BulkResult;
import io.searchbox.core.ESJestBulkActions;
import org.syslog_ng.elasticsearch_v2.ElasticSearchOptions;
import org.syslog_ng.elasticsearch_v2.client.http.ESHttpClient;

import java.io.*;
import java.util.ArrayList;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import java.util.function.Supplier;

public class HttpBulkMessageProcessor extends HttpMessageProcessor {

  private volatile ESJestBulkActions bulk;
  private ESJestHttpClient httpClient;
  private int flushLimit;

  private final String name;
  private final ArrayList<Thread> senders;
  private final BlockingQueue<ESJestBulkActions> messageQueue;
  private final int concurrency;
  private volatile boolean shutDown = false;

  private boolean isInitialized = false;

  private int enqueueFailCount = 0;
  private long enqueueStartTime = 0;
  private final int enqueueTimeout;
  private final boolean debugEnabled;

  public HttpBulkMessageProcessor(ElasticSearchOptions options, ESHttpClient client) {
    super(options, client);
    bulk = null;
    concurrency = options.getConcurrentRequests();
    final String qSzTimes = System.getProperty("BulkRequestQSzTimes",
            Integer.toString(10));
    messageQueue = new ArrayBlockingQueue<>(concurrency * Integer.valueOf(qSzTimes));
    senders = new ArrayList<>(concurrency);
    name = options.getIdentifier().getValue();
    enqueueTimeout = Integer.parseInt(System.getProperty("EnqueueTimeOutMS", "100"));
    debugEnabled = logger.isDebugEnabled() ; //name.indexOf("stats") > 0;
  }

  @Override
  public void init() {

    flushLimit = options.getFlushLimit();
    shutDown = false;
    assert client.getClient() != null : "Client must be initialized by this time.";

    for (int i = 0; i < concurrency; i++) {
      senders.add(new Thread(() -> {
        ESJestBulkActions bulkActions;
        int exceptionCount = 0;
        int httpRequestErrorCount = 0;

        try {
          while (!shutDown) {
              if ((bulkActions = messageQueue.poll(
                      100, TimeUnit.MILLISECONDS)) == null) {
                if(logger.isTraceEnabled()) {
                  logmsg(() -> "No events found in the message queue. " + messageQueue.size());
                }
                continue;
              }

              final BulkResult jestResult;
              try {
                jestResult = client.getClient().execute(bulkActions);
                exceptionCount = 0;
              } catch (Exception e) {
                if (exceptionCount++ < 3) {
                  logger.error("[ " + exceptionCount + "] Exception in bulk execution: "
                          + e.getMessage(), e);
                } else if (exceptionCount < 20) {
                  logger.info("Exception in bulk execution: " + e.getMessage());
                } else if (logger.isDebugEnabled()) {
                  logger.debug("Exception in bulk execution: " + e.getMessage());
                }
                client.incDroppedBatchCounter(bulkActions.getRowCount());
                continue;
              } catch (Throwable t) {
                StringWriter sw = new StringWriter();
                PrintWriter printer = new PrintWriter(sw);
                t.printStackTrace(printer);
                logger.error(name + " Unexpected Exception : " + t.getClass().getSimpleName()
                        + " " + t.getMessage() + "\n" + sw.toString(), t);
                continue;
              }

              // if clientRequest.isSuccessFull declares true, jestResult == null
              // indicates the request is success and here we DONOT want to completely deserialize
              // the http response.
              if (jestResult != null && !jestResult.isSucceeded()) {
                StringBuilder sb = new StringBuilder();
                jestResult.getFailedItems().forEach(action ->
                        sb.append(action.id).append("=").append(action.error).append("\n"));
                if (httpRequestErrorCount++ < 3)
                  logger.warn(jestResult.getErrorMessage() + ". FailedItem Details: \n" + sb.toString());
                else if (logger.isDebugEnabled())
                  logger.debug(jestResult.getErrorMessage() + ". FailedItem Details: \n" + sb.toString());
                //SB: TEMP failedRequests.offer(bulkActions);
                client.incDroppedBatchCounter(bulkActions.getRowCount());
                continue;
              }

              if (httpRequestErrorCount > 0) {
                logger.warn(name + " http error " + httpRequestErrorCount + " occurred.");
                httpRequestErrorCount = 0;
              }

              if (debugEnabled && jestResult != null) {
                logmsg(() -> " processed jest result [" + jestResult.getJsonString() + "]");
              }
          } // end of while
        } catch (InterruptedException e) {
          if (!shutDown) {
            logger.error(name + " Bulk Execution Thread interrupted without shutdown initiated", e);
          }
        } catch (Exception e) {
          logger.error(name + " Exception Occurred : " + e.getClass().getSimpleName()
                  + " " + e.getMessage(), e);
        } catch (Throwable t) {
          StringWriter sw = new StringWriter();
          PrintWriter printer = new PrintWriter(sw);
          t.printStackTrace(printer);
          logger.error(name + " Unknown Exception : " + t.getClass().getSimpleName()
                  + " " + t.getMessage() + "\n" + sw.toString(), t);
        } finally {
          bulkActions = null;
        }
      }, name + "-HttpSender-" + i));

      isInitialized = true;
    }

    senders.forEach(t -> {
      logger.info("Starting sender " + t.getName());
      t.start();
    });

  }

  @Override
  public boolean flush() {
    if (debugEnabled) {
      logmsg(() -> "Flushing messages for ES destination [mode=" + options.getClientMode() + "] -- "
              + (bulk != null ? bulk.getRowCount() : "bulk message NULL, flush will return false. "));
    }
    try {
      if (bulk != null) {
        return scheduleBulkAction(bulk);
      }
    } finally {
      bulk = null;
    }

    return true;
  }

  @Override
  public void deinit() {
    logmsg(() -> "HttpBulkMessageProcessor deinit called... shutting down the threadpool");
    shutDown = true;
    senders.forEach(t -> {
      logmsg(() -> "Waiting for " + t.getName() + " to terminate");
      try {
        t.join(10000);
      } catch (InterruptedException e) {
        logger.error(name + " " + Thread.currentThread() + " " +
                t.getName() + " interrupted while waiting for termination.", e);
      }
    });
    senders.clear();
  }

  private boolean scheduleBulkAction(final ESJestBulkActions bulkActions) {
    try {
      if (!messageQueue.offer(bulkActions, enqueueTimeout, TimeUnit.MILLISECONDS)) {
        if(enqueueFailCount <= 0) {
          if(logger.isDebugEnabled()) {
            logger.debug(Thread.currentThread().getId() + " " + name + " " + enqueueTimeout + " millis elapsed to schedule bulk request. " +
                    "Queue full with " + messageQueue.size() + " messages. " +
                    "Consider increasing concurrent_request or elastic cluster capacity.");
          }
          enqueueStartTime = System.currentTimeMillis() + enqueueTimeout;
        }

        enqueueFailCount++;
        client.incDroppedBatchCounter(bulkActions.getRowCount());
        return true;
      } else if (enqueueFailCount != 0) {
        logmsg(() -> " " + name + " " + enqueueFailCount +
                " enqueue attempts failed and " + (System.currentTimeMillis() - enqueueStartTime) +
                " millis elapsed since last success.");
        enqueueFailCount = 0;
        enqueueStartTime = 0;
        return true;
      }

    } catch (InterruptedException ie) {
      // ignore
      return false;
    }

    return true;
  }

  private static Object addIndex(HttpBulkMessageProcessor processor,
                                 String index,
                                 String type,
                                 String id,
                                 String pipeline,
                                 String formattedMessage) {

    if (processor.bulk == null) {
      processor.bulk = new ESJestBulkActions(index, type, pipeline, processor.logger.isDebugEnabled());
    }
    processor.bulk.addMessage(formattedMessage, index, type, pipeline, id);

    return processor.bulk;
  }

  @Override
  public boolean sendImpl(Function<IndexFieldHandler, Object> msgBuilder) {
    msgBuilder.apply((index, type, id, pipeline, formattedMessage) ->
            addIndex(this, index, type, id, pipeline, formattedMessage));
    return true;
  }

  private void logmsg(Supplier<String> msg) {
    logger.info(name + " " + msg.get());
  }
}
