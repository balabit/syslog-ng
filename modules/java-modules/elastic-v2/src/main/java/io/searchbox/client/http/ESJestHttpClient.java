package io.searchbox.client.http;

import io.searchbox.action.Action;
import io.searchbox.client.JestResult;
import io.searchbox.client.JestResultHandler;
import io.searchbox.core.ESJestBulkActions;
import org.apache.commons.lang3.StringUtils;
import org.apache.http.*;
import org.apache.http.client.methods.HttpUriRequest;
import org.apache.http.concurrent.FutureCallback;
import org.apache.http.entity.ContentType;
import org.apache.http.impl.nio.client.CloseableHttpAsyncClient;
import org.apache.http.protocol.HTTP;
import org.apache.http.util.CharArrayBuffer;
import org.apache.http.util.EntityUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.*;
import java.nio.charset.Charset;
import java.nio.charset.UnsupportedCharsetException;
import java.util.Map;

import static io.searchbox.core.ESJestBulkActions.BLANK;

public class ESJestHttpClient extends JestHttpClient {
  private final static Logger log = LoggerFactory.getLogger(ESJestHttpClient.class);

  @Override
  public <T extends JestResult> T execute(Action<T> clientRequest) throws IOException {
    if (log.isDebugEnabled()) {
      log.debug("SB: [{}] About to send {}", Thread.currentThread().getName(), clientRequest.getURI());
    }
    HttpUriRequest request = prepareRequest(clientRequest);
    HttpResponse response = getHttpClient().execute(request);
    if (log.isDebugEnabled()) {
      log.debug("SB: [{}] Received response {}", Thread.currentThread().getName(), clientRequest.getURI());
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
            log.debug("SB: [{}] Message successfully Processed {}", Thread.currentThread().getName(),
                    clientRequest.getURI());
          }
          return null;
        }
      } catch (IOException e) {
        log.warn("SB: Got exception response while executing request " + e.getMessage(), e);
        throw e;
      }
    }

    return deserializeResponse(response, request, clientRequest, firstFewLines);
  }

  @Override
  protected <T extends JestResult> HttpUriRequest prepareRequest(final Action<T> clientRequest) {
    String elasticSearchRestUrl = getRequestURL(getNextServer(), clientRequest.getURI());
    HttpUriRequest request = constructHttpMethod(clientRequest.getRestMethodName(), elasticSearchRestUrl, clientRequest.getData(gson));

    log.debug("Request method={} url={}", clientRequest.getRestMethodName(), elasticSearchRestUrl);

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
