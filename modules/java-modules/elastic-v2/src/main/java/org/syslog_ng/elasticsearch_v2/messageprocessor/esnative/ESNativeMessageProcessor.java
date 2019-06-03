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

package org.syslog_ng.elasticsearch_v2.messageprocessor.esnative;

import org.apache.log4j.Logger;
import org.elasticsearch.action.index.IndexRequest;
import org.syslog_ng.elasticsearch_v2.ElasticSearchOptions;
import org.syslog_ng.elasticsearch_v2.client.esnative.ESNativeClient;
import org.syslog_ng.elasticsearch_v2.messageprocessor.ESIndex;
import org.syslog_ng.elasticsearch_v2.messageprocessor.ESMessageProcessor;
import org.syslog_ng.elasticsearch_v2.messageprocessor.http.IndexFieldHandler;

import java.util.function.Function;

public abstract class ESNativeMessageProcessor implements ESMessageProcessor {
	protected ElasticSearchOptions options;
	protected ESNativeClient client;
	protected Logger logger;


	public ESNativeMessageProcessor(ElasticSearchOptions options, ESNativeClient client) {
		this.options = options;
		this.client = client;
		logger = Logger.getRootLogger();
	}

	public void init() {

	}

	public void flush() {

	}

	public void deinit() {

	}

	protected abstract boolean send(IndexRequest req);

	@Override
	public boolean send(Function<IndexFieldHandler, Object> messageBuilder) {

		IndexRequest req = (IndexRequest) messageBuilder.apply(((index, type, id, pipeline, formattedMessage) -> {
			return new IndexRequest(index, type, id).source(formattedMessage);
		}));

		return send(req);
	}

}
