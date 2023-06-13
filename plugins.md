# Plugins

VFVM has first-class support for plugins.

As of now, there is only one- partial match output, which prints progressively longer prefix matches as the target address is mined.

## Developing Plugins

Please see [examples](plugins/examples) for a simple example that just adds some logging.

To run an example, copy its directory into plugins/ before compiling with `make PLUGINS=true`

### Memory Management

Allocating memory is slow, and therefor ought only be done on a plugin's `on_init` or constructor-attribute path.

Any allocated memory **must** be released in the destructor-attribute path, the plugin will not be accepted.

### Thread Safety

Note that VFVM uses pthreads to scale.
This means that `on_iteration` is called concurrently, and must therefor be thread-safe.

Incidentally, because `on_iteration` is called concurrently, `on_progress` must not access memory in an unsafe manner.
