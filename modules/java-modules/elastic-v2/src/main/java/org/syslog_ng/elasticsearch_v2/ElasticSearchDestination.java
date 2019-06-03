/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Viktor Juhasz <viktor.juhasz@balabit.com>
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

package org.syslog_ng.elasticsearch_v2;

import org.apache.log4j.Logger;
import org.syslog_ng.LogMessage;
import org.syslog_ng.LogTemplate;
import org.syslog_ng.StructuredLogDestination;
import org.syslog_ng.elasticsearch_v2.client.ESClient;
import org.syslog_ng.elasticsearch_v2.client.ESClientFactory;
import org.syslog_ng.elasticsearch_v2.client.UnknownESClientModeException;
import org.syslog_ng.elasticsearch_v2.messageprocessor.http.IndexFieldHandler;
import org.syslog_ng.logging.SyslogNgInternalLogger;
import org.syslog_ng.options.InvalidOptionException;

import java.util.function.Function;

public class ElasticSearchDestination extends StructuredLogDestination {

	ESClient client;
	ElasticSearchOptions options;
	Logger logger;

	volatile boolean opened;

	public ElasticSearchDestination(long handle) {
		super(handle);
		logger = Logger.getRootLogger();
		SyslogNgInternalLogger.register(logger);
		options = new ElasticSearchOptions(this);
  }

	@Override
	protected boolean init() {
		logger.warn("Using elasticsearch2() destination is deprecated please use elasticsearch-http() instead. The java-based elasticsearch2() works as before but it may be removed in the future");
		boolean result = false;
		try {
			options.init();

                        setBatchLines(options.getFlushLimit());
                        setBatchTimeout(options.getFlushTimeout());

			client = ESClientFactory.getESClient(options);
			client.init(this::incDroppedMessages);
			result = true;
		}
		catch (InvalidOptionException | UnknownESClientModeException e){
			logger.error(e.getMessage());
			return false;
		}
		return result;
	}

	@Override
	protected boolean isOpened() {
		return opened;
	}

	@Override
	protected boolean open() {
		opened = client.open();
		return opened;
	}

	private static Object createIndexRequest(ElasticSearchDestination dest,
																					 LogMessage msg,
																					 IndexFieldHandler fieldsHandler) {
		long templateHandler = dest.getTemplateOptionsHandle();
		ElasticSearchOptions options = dest.options;
		String formattedMessage = options.getMessageTemplate().getResolvedString(msg, templateHandler, LogTemplate.LTZ_SEND);
		String customId = options.getCustomId().getResolvedString(msg, templateHandler, LogTemplate.LTZ_SEND);
		String index = options.getIndex().getResolvedString(msg, templateHandler, LogTemplate.LTZ_SEND);
		String type = options.getType().getResolvedString(msg, templateHandler, LogTemplate.LTZ_SEND);
		String pipeline = options.getPipeline().getResolvedString(msg, templateHandler, LogTemplate.LTZ_SEND);

		return fieldsHandler.handle(index, type, customId, pipeline, formattedMessage);
	}

	private int send_batch(Function<IndexFieldHandler, Object> index) {
		if (client.send(index))
			return QUEUED;
		else
			return ERROR;
	}

	private int send_single(Function<IndexFieldHandler, Object> index) {
		if (client.send(index))
			return SUCCESS;
		else
			return ERROR;
	}

	@Override
	protected int send(LogMessage msg) {
		try {
      if (!client.isOpened()) {
        return NOT_CONNECTED;
      }

      Function<IndexFieldHandler, Object> index = f -> createIndexRequest(this, msg, f);
      if (options.getFlushLimit() > 1) {
        return send_batch(index);
      }
      else {
        return send_single(index);
      }
    } catch(Exception e) {
		  logger.error("Exception in ES destination during send: " + e.getMessage(), e);
		  throw e;
    }
  }

	@Override
	protected void close() {
		if (opened) {
			client.close();
			opened = false;
		}
	}

	@Override
	protected String getNameByUniqOptions() {
		return String.format("ElasticSearch_v2,%s,%s,%s", client.getClusterName(),
						options.getIndex().getValue(), options.getIdentifier().getValue());
	}

	@Override
	protected void deinit() {
		client.deinit();
		options.deinit();
	}

	@Override
	public int flush() {
    try {
      if (client.flush())
        return SUCCESS;
      else
        return ERROR;
    } catch (Exception e) {
      logger.error("Exception in ES destination during flush: " + e.getMessage(), e);
			return ERROR;
    }
	}
}
