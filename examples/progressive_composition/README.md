# Progressive composition

This strategy is primarily for WASM.

## Logic

### Sublayer

* First collect asset names in 'subLayer' Stage(Layer) metadata.
* Resolve assets in the app.
* Parse asset(USD)
* Composite subLayer
  * Assert resolver call in C++ compositer is stay as the same
* Iterate until no 'subLayer' Stage(Layer) metadata found.
