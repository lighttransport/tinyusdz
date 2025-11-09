# Run this script from <tinyusdz> root.


podman run --rm --userns=keep-id -v `pwd`:/home/ubuntu/workspace  -v $HOME/.claude.json:/home/ubuntu/.claude.json -v $HOME/.claude:/home/ubuntu/.claude -it tinyusdz claude
#podman run --rm -v `pwd`:/home/ubuntu/workspace  -v $HOME/.claude.json:/home/ubuntu/.claude.json -v $HOME/.claude:/home/ubuntu/.claude -it tinyusdz bash
