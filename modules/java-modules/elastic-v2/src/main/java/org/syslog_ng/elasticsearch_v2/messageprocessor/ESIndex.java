/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Laszlo Budai <laszlo.budai@balabit.com>
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

package org.syslog_ng.elasticsearch_v2.messageprocessor;

import org.apache.commons.lang3.StringUtils;

public class ESIndex {
	private String formattedMessage;
	private String index;
	private String type;
	private String id;
	private String pipeline;

	public static class Builder {
		String formattedMessage;
		String index;
		String type;
		String id;
		String pipeline;

		public Builder formattedMessage(String formattedMessage) {
			Builder.this.formattedMessage = formattedMessage;
			return Builder.this;
		}

		public Builder index(String index) {
			Builder.this.index = index;
			return Builder.this;
		}

		public Builder pipeline(String pipeline) {
			Builder.this.pipeline = pipeline;
			return Builder.this;
		}

		public Builder type(String type) {
			Builder.this.type = type;
			return Builder.this;
		}

		public Builder id(String id) {
			Builder.this.id = id;
			return Builder.this;
		}

		public ESIndex build() {
			return new ESIndex(Builder.this);
		}
	}

	private ESIndex(Builder builder) {
		this.formattedMessage = builder.formattedMessage;
		this.index = builder.index;
		this.type = builder.type;
		this.id = builder.id;
  	this.pipeline = builder.pipeline;
	}

	public String getFormattedMessage() {
		return formattedMessage;
	}

	public String getIndex() {
		return index;
	}

	public String getType() {
		return type;
	}

	public String getPipeline() { return pipeline; }

	public String getId() {
		return id;
	}
}
