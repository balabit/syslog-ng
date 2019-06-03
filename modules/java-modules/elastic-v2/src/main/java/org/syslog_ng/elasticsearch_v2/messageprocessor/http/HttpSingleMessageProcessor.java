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

import io.searchbox.core.Index;
import io.searchbox.client.JestResult;
import io.searchbox.core.JestIndex;
import org.syslog_ng.elasticsearch_v2.ElasticSearchOptions;
import org.syslog_ng.elasticsearch_v2.client.http.ESHttpClient;

import java.io.IOException;
import java.util.function.Function;

public class HttpSingleMessageProcessor extends  HttpMessageProcessor {

	public HttpSingleMessageProcessor(ElasticSearchOptions options, ESHttpClient client) {
		super(options, client);
	}

	@Override
	public boolean sendImpl(Function<IndexFieldHandler, Object> msgBuilder) {

		JestIndex req = (JestIndex) msgBuilder.apply((index, type, id, pipeline, formattedMessage) ->
						new JestIndex().setIndexName(index).setType(type).setId(id)
						.setFormattedMessage(formattedMessage));

	  JestResult jestResult = null;
		try {
			jestResult = client.getClient().execute(req);
		}
		catch (IOException e)
		{
			logger.error(e.getMessage());
			return false;
		}
		if (! jestResult.isSucceeded()) {
			logger.error(jestResult.getErrorMessage());
			return false;
		}
		return true;
	}

}

