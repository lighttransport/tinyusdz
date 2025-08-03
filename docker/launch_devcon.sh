# Run this script from <tinyusdz> root.


podman run -v `pwd`:/workspace  -v $HOME/.claude:/root/.claude -it tinyusdz bash
