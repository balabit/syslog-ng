`redis()`: add `workers()` support (multi-threaded connection pooling)

The Redis driver now support the `workers()` opttion, which specifies the
number of parallel workers to be used.

This increases the throughput of the Redis destination driver.

Example:

```
destination d_redis {
  redis(
      host("localhost")
      port(6379)
      command("HINCRBY", "hosts", "$HOST", "1")
      workers(8)
      );
};
```
