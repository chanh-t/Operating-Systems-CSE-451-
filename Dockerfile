# Dockerfile created by 21wi student, provided here with permission
# Requires Docker install (i.e. won't run on attu)
# TAs will not teach you Docker or debug issues with Docker - use at your own risk.
# =======


# Install instructions:
#
# 1. Install docker: `sudo apt install docker`
# 2. Build the image: `sudo docker build -t xk .`

# Terminal usage instructions:
#
# 1. Prepare to save your terminal history across sessions:
#    `mkdir -p ~/.cache/docker && touch ~/.cache/docker/bash_history`
# 2. Run the image:
#    sudo docker run --rm -it -p25000:25000 \
#      -v "$(pwd):/home/xk" \
#      -v "$HOME/.cache/docker/bash_history:/root/.bash_history" \
#      --name xk xk bash
# 3. (Optional) Start another shell: `sudo docker exec -it xk bash`

# CLion usage instructions:
#
# 1. In `.gdbinit.tmpl`, remove the two "target remote" lines and the main breakpoint
# 2. Follow the remote gdb steps:
#    https://www.jetbrains.com/help/clion/remote-debug.html.
#    You only need to configure the args as `localhost:25000`.
# 3. Inside a running container, start debugging qemu with `make qemu-gdb` as usual. Then back in
#    CLion, hit debug and enjoy.

FROM tianon/qemu:5.1

RUN echo 'deb http://deb.debian.org/debian testing main' >> /etc/apt/sources.list

RUN apt-get update \
  && apt-get install -y \
    gcc-9 \
    gdb \
    make \
    python2 \
    procps \
  && apt-get clean
RUN ln -s "$(which gcc-9)" /usr/bin/gcc

RUN echo "add-auto-load-safe-path /home/xk/.gdbinit" > ~/.gdbinit

WORKDIR /home/xk
CMD ["make", "qemu-gdb"]

