# wlr-protocols

From https://gitlab.freedesktop.org/wlroots/wlr-protocols (`unstable/`).

Local change: in `wlr-layer-shell-unstable-v1.xml` the `get_layer_surface`
argument `namespace` is renamed to `layer_namespace`, as `namespace` is a C++
keyword. This doesn't affect the wire protocol.
