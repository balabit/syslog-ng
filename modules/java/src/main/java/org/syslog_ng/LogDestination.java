/*
 * Copyright (c) 2014 Balabit
 * Copyright (c) 2014 Viktor Juhasz <viktor.juhasz@balabit.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

package org.syslog_ng;

public abstract class LogDestination extends LogPipe {

	protected static final int DROP = 0;
	protected static final int ERROR = 1;
	protected static final int SUCCESS = 3;
	protected static final int QUEUED = 4;
	protected static final int NOT_CONNECTED = 5;
	protected static final int RETRY = 6;

	public LogDestination(long pipeHandle) {
		super(pipeHandle);
	}

	public String getOption(String key) {
		return getOption(getHandle(), key);
	}

	public long getTemplateOptionsHandle() {
		return getTemplateOptionsHandle(getHandle());
	}

	public int getSeqNum() {
 		return getSeqNum(getHandle());
        }

	public int getBatchLines() {
 		return getBatchLines(getHandle());
        }

	public void setBatchLines(long batch_size) {
 		setBatchLines(getHandle(), batch_size);
        }

	public void setBatchTimeout(long timeout) {
 		setBatchTimeout(getHandle(), timeout);
        }

  public void incDroppedMessages(int batch_size) {
    incDroppedMessages(getHandle(), batch_size);
  }

	protected abstract boolean open();

	protected abstract void close();

	protected abstract boolean isOpened();

	protected abstract String getNameByUniqOptions();

	private native String getOption(long ptr, String key);

  private native long getTemplateOptionsHandle(long ptr);

	private native int getSeqNum(long ptr);

	private native int getBatchLines(long ptr);

	private native int setBatchLines(long ptr, long batch_size);

	private native int setBatchTimeout(long ptr, long timeout);

	protected int flush() {
		return SUCCESS;
	}

  private native void incDroppedMessages(long ptr, int batch_size);

	public boolean openProxy() {
		try {
			return open();
		}
		catch (Exception e) {
			sendExceptionMessage(e);
			return false;
		}
	}

	public void closeProxy() {
		try {
			close();
		}
		catch (Exception e) {
			sendExceptionMessage(e);
		}
	}

	public boolean isOpenedProxy() {
		try {
			return isOpened();
		}
		catch (Exception e) {
			sendExceptionMessage(e);
			return false;
		}
	}

	public int flushProxy() {
		try {
			return flush();
		}
		catch (Exception e) {
			sendExceptionMessage(e);
			return ERROR;
		}
	}

	public String getNameByUniqOptionsProxy() {
		try {
			return getNameByUniqOptions();
		}
		catch (Exception e) {
			sendExceptionMessage(e);
			return null;
		}
	}

}
