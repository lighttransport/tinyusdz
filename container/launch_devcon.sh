# Run this script from <lightusd> root.


podman run --rm --userns=keep-id -v `pwd`:/home/ubuntu/workspace  -v $HOME/.claude.json:/home/ubuntu/.claude.json -v $HOME/.claude:/home/ubuntu/.claude -it lightusd claude
#podman run --rm -v `pwd`:/home/ubuntu/workspace  -v $HOME/.claude.json:/home/ubuntu/.claude.json -v $HOME/.claude:/home/ubuntu/.claude -it lightusd bash
