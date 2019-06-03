/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Laszlo Budai <laszlo.budai@balabit.com>
 *
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

package io.searchbox.client.http;

import io.searchbox.action.Action;
import io.searchbox.client.JestResult;
import io.searchbox.client.JestResultHandler;
import io.searchbox.cluster.NodesInfo;
import io.searchbox.core.ESJestBulkActions;
import org.apache.commons.lang3.StringUtils;
import org.apache.http.*;
import org.apache.http.client.methods.HttpUriRequest;
import org.apache.http.entity.ContentType;
import org.apache.http.impl.nio.client.CloseableHttpAsyncClient;
import org.apache.http.protocol.HTTP;
import org.apache.http.util.CharArrayBuffer;
import org.apache.http.util.EntityUtils;
import org.apache.log4j.Logger;
import org.elasticsearch.action.bulk.BulkAction;

import java.io.*;
import java.nio.charset.Charset;
import java.nio.charset.UnsupportedCharsetException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class ESJestHttpClient extends JestHttpClient {
  private final static Logger log = org.apache.log4j.Logger.getRootLogger();

  @Override
  public <T extends JestResult> T execute(Action<T> clientRequest) throws IOException {
    if (log.isDebugEnabled()) {
      log.debug("["+ Thread.currentThread().getName() + "] About to send " + clientRequest.getURI());
    }
    HttpUriRequest request = prepareRequest(clientRequest);
    HttpResponse response = getHttpClient().execute(request);
    if (log.isDebugEnabled()) {
      log.debug("["+ Thread.currentThread().getName() + "] Received response " + clientRequest.getURI());
    }
    String firstFewLines = null;
    if (clientRequest instanceof ESJestBulkActions) {
      try {
        firstFewLines = getFirstFewLines(response);
        if (StringUtils.isNotBlank(firstFewLines) &&
                ((ESJestBulkActions)clientRequest)
                        .isSuccessFull(firstFewLines, response.getStatusLine().getStatusCode())) {
          //EntityUtils.consumeQuietly(response.getEntity());
          try {
            HttpEntity e = response.getEntity();
            if (e != null) {
              InputStream s = e.getContent();
              if (s != null) {
                s.close();
              }
            }
          } catch (Exception e) {
            //ignore
            ;
          }
          if (log.isDebugEnabled()) {
            log.debug("["+ Thread.currentThread().getName() + "] Message successfully Processed " + clientRequest.getURI());
          }
          return null;
        }
      } catch (IOException e) {
        log.warn("Got exception response while executing request " + e.getMessage(), e);
        throw e;
      }
    }

    return deserializeResponse(response, request, clientRequest, firstFewLines);
  }

  private Pattern getURI = Pattern.compile("(^(http[s]?:/{2})?[^/]*).*$",Pattern.CASE_INSENSITIVE);

  @Override
  protected <T extends JestResult> HttpUriRequest prepareRequest(final Action<T> clientRequest) {
    HttpUriRequest request = null;
    try {
      String nextServerUrl = getNextServer();
      if (!(clientRequest instanceof ESJestBulkActions)) {
        final Matcher m = getURI.matcher(nextServerUrl);
        if (m.matches() && m.groupCount() > 1) {
          nextServerUrl = m.group(1);
        }
      }
      String elasticSearchRestUrl = getRequestURL(nextServerUrl, clientRequest.getURI());
      request = constructHttpMethod(clientRequest.getRestMethodName(), elasticSearchRestUrl, clientRequest.getData(gson));

      log.debug("Request method=" + clientRequest.getRestMethodName() + " url=" + elasticSearchRestUrl);
    } catch(Exception e) {
      log.error("Error preparing request " + e.getMessage(), e);
      throw e;
    }
    return request;
  }

  @Override
  public <T extends JestResult> void executeAsync(final Action<T> clientRequest, final JestResultHandler<? super T> resultHandler) {
    CloseableHttpAsyncClient asyncClient = getAsyncClient();

    synchronized (this) {
      if (!asyncClient.isRunning()) {
        asyncClient.start();
      }
    }

    HttpUriRequest request = prepareRequest(clientRequest);
    asyncClient.execute(request, new ESHttpCallback<T>(clientRequest, request, resultHandler));
  }

  protected class ESHttpCallback<T extends JestResult> extends DefaultCallback<T>  {
    private final Action<T> clientRequest;
    private final HttpRequest request;
    private final JestResultHandler<? super T> resultHandler;

    ESHttpCallback(Action<T> clientRequest,
                          final HttpRequest request,
                          JestResultHandler<? super T> resultHandler) {
      super(clientRequest, request, resultHandler);
      this.clientRequest = clientRequest;
      this.request = request;
      this.resultHandler = resultHandler;
    }

    @Override
    public void completed(final HttpResponse response) {
      String firstFewLines = null;
      if (clientRequest instanceof ESJestBulkActions) {
        try {
          firstFewLines = getFirstFewLines(response);
          if (StringUtils.isNotBlank(firstFewLines) &&
                  ((ESJestBulkActions)clientRequest)
                          .isSuccessFull(firstFewLines, response.getStatusLine().getStatusCode())) {
            EntityUtils.consumeQuietly(response.getEntity());
            return;
          }
        } catch (IOException e) {
          failed(e);
        }
      }

      T jestResult = null;
      try {
        jestResult = deserializeResponse(response, request, clientRequest, firstFewLines);
      } catch (IOException e) {
        failed(e);
      }
      if (jestResult != null) resultHandler.completed(jestResult);
    }
  }

  private String getFirstFewLines(HttpResponse response) throws IOException {
    StatusLine statusLine = response.getStatusLine();
    HttpEntity entity = response.getEntity();
    if(statusLine.getStatusCode() != 200 || entity == null) {
      return null;
    }

    final InputStream instream = entity.getContent();
    if (instream == null) {
      return null;
    }

    Charset charset = null;
    try {
      final ContentType contentType = ContentType.get(entity);
      if (contentType != null) {
        charset = contentType.getCharset();
      }
    } catch (final UnsupportedCharsetException ex) {
      // will get caught at EntityUtils.toString.
      return null;
    }
    if (charset == null) {
      charset = HTTP.DEF_CONTENT_CHARSET;
    }
    final Reader reader = new InputStreamReader(instream, charset);
    final CharArrayBuffer buffer = new CharArrayBuffer(1024);
    final char[] tmp = new char[1024];
    int l;
    if ((l = reader.read(tmp)) != -1) {
      buffer.append(tmp, 0, l);
    }
    return buffer.toString();
  }

  private <T extends JestResult> T deserializeResponse(HttpResponse response,
                                                       final HttpRequest httpRequest,
                                                       Action<T> clientRequest,
                                                       final String firstFewLines) throws IOException {
    StatusLine statusLine = response.getStatusLine();
    try {
      return clientRequest.createNewElasticSearchResult(
                      (response.getEntity() == null ? null :
                              firstFewLines != null ? firstFewLines + EntityUtils.toString(response.getEntity()) :
                                      EntityUtils.toString(response.getEntity())),
              statusLine.getStatusCode(),
              statusLine.getReasonPhrase(),
              gson
      );
    } catch (com.google.gson.JsonSyntaxException e) {
      for (Header header : response.getHeaders("Content-Type")) {
        final String mimeType = header.getValue();
        if (!mimeType.startsWith("application/json")) {
          // probably a proxy that responded in text/html
          final String message = "Request " + httpRequest.toString() + " yielded " + mimeType
                  + ", should be json: " + statusLine.toString();
          throw new IOException(message, e);
        }
      }
      throw e;
    }
  }

}
