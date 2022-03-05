# gst-timecode

Build:
```
meson builddir
ninja -C builddir
```

To use the plugin:
```
export GST_PLUGIN_PATH="$(pwd)/builddir/"
gst-inspect-1.0 timecodeoverlay
gst-inspect-1.0 timecodeparse
```
