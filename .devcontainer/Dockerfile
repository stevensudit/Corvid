# gcc 16.2 release, built from source. The ubuntu-toolchain-r/test PPA only
# carries a March 2026 trunk snapshot of gcc 16 for noble, so the release comes
# from the gcc.gnu.org tarball instead. Its own stage so the layer cache
# survives edits to the main image below; --disable-bootstrap halves the build
# (a bootstrap only guards against a broken host compiler). The prefix is
# /opt/gcc-16 rather than /usr/local so it cannot collide with /usr/local/cuda.
#
# The source gets one local patch, gcc-constexpr-strret.patch: the C++ front
# end's constexpr evaluator counts a nonzero offset twice in the pointer that
# memchr/strchr/strrchr/strstr return, which C++26 exposes through
# std::string_view::find on constant haystacks (details in the patch header
# and crossplatform.md). gcc_constexpr_strret_check.cpp is compiled against
# the installed gcc so the image build fails if the patch did not take. After
# a GCC_VERSION bump, a patch that no longer applies means upstream fixed it:
# drop the patch and the check together.
FROM ubuntu:24.04 AS gcc-build
ARG GCC_VERSION=16.2.0
# SHA-512 of gcc-${GCC_VERSION}.tar.xz, from the release's sha512.sum on
# gcc.gnu.org, cross-checked against the bytes ftp.gnu.org serves. Pins the
# exact source the image builds, since HTTPS alone trusts whatever the server
# sends. A version bump updates both ARGs.
ARG GCC_SHA512=c51c30ca7422d0cbecf504b2e0f33c3aca31e0f90a76b65217f465163fa6fa17b3f5de39e145c47e5bab90ac0ce7fff3b03c8d553ae36e01faaea5a50f8648d1
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl xz-utils build-essential flex bison patch \
    libgmp-dev libmpfr-dev libmpc-dev libisl-dev zlib1g-dev libzstd-dev && \
    rm -rf /var/lib/apt/lists/*
COPY gcc-constexpr-strret.patch gcc_constexpr_strret_check.cpp /tmp/corvid/
RUN curl -fsSL -o /tmp/gcc-${GCC_VERSION}.tar.xz \
      https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz && \
    echo "${GCC_SHA512}  /tmp/gcc-${GCC_VERSION}.tar.xz" | sha512sum -c - && \
    tar -xJ -C /tmp -f /tmp/gcc-${GCC_VERSION}.tar.xz && \
    rm /tmp/gcc-${GCC_VERSION}.tar.xz && \
    patch -p1 -d /tmp/gcc-${GCC_VERSION} < /tmp/corvid/gcc-constexpr-strret.patch && \
    mkdir /tmp/gcc-obj && cd /tmp/gcc-obj && \
    /tmp/gcc-${GCC_VERSION}/configure --prefix=/opt/gcc-16 \
      --enable-languages=c,c++ --disable-multilib --disable-bootstrap \
      --with-system-zlib --enable-default-pie --enable-default-ssp \
      --program-suffix=-16 && \
    make -j"$(nproc)" && make install-strip && rm -rf /tmp/gcc-* && \
    /opt/gcc-16/bin/g++-16 -std=c++23 -fsyntax-only /tmp/corvid/gcc_constexpr_strret_check.cpp && \
    rm -rf /tmp/corvid

FROM nvidia/cuda:13.3.1-devel-ubuntu24.04

ARG TZ
ENV TZ="$TZ"
ARG DEBIAN_FRONTEND=noninteractive
ARG CLAUDE_CODE_VERSION=latest
ARG USERNAME=node

# Un-minimize: base image strips docs/man pages at dpkg level; we want them.
RUN rm -f /etc/dpkg/dpkg.cfg.d/excludes && \
    if dpkg-divert --list /usr/bin/man | grep -q .; then \
      rm -f /usr/bin/man && \
      dpkg-divert --quiet --remove /usr/bin/man; \
    fi

# Ubuntu 24.04 ships a default 'ubuntu' user at UID 1000; replace it with
# 'node' at the same UID so existing volume file ownership carries over.
RUN userdel -r ubuntu 2>/dev/null || true && \
    useradd -m -u 1000 -U -s /bin/bash node

# Install clang repo requirements and prerequisites.
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl wget lsb-release gnupg software-properties-common && \
    wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && \
    ./llvm.sh 23 && rm llvm.sh && \
    rm -rf /var/lib/apt/lists/*

# Generate en_US.UTF-8, so bash stops complaining.
RUN apt-get update && apt-get install -y --no-install-recommends locales && \
    sed -i 's/^# *en_US.UTF-8/en_US.UTF-8/' /etc/locale.gen && locale-gen && \
    rm -rf /var/lib/apt/lists/*

# Test toolchain repo.
RUN add-apt-repository -y ppa:ubuntu-toolchain-r/test

# Node.js was the base image; on the CUDA base it's an install step.
RUN curl -fsSL https://deb.nodesource.com/setup_24.x | bash -

# Basic development tools, toolchain, and iptables/ipset. gcc-15 is the nvcc
# host compiler (nvcc 13.3 rejects gcc newer than 15; see cleanbuild.sh). The
# PPA's gcc-16 snapshot stays only for its libstdc++-16-dev, which the clang +
# libstdc++ leg picks up from /usr/lib/gcc (its <format> gets a local patch
# below); the gcc leg itself uses the 16.2 built in the gcc-build stage. catch2
# is the system libstdc++ Catch2 that the
# CUDA tests link against (tests/CMakeLists.txt requires it once nvcc and
# g++-15 are both present; the .cpp suite uses its own FetchContent Catch2).
RUN apt-get update && apt-get install -y --no-install-recommends \
  less \
  git \
  procps \
  sudo \
  fzf \
  zsh \
  man-db \
  unzip \
  gnupg2 \
  gh \
  iptables \
  ipset \
  iproute2 \
  dnsutils \
  aggregate \
  jq \
  nano \
  vim \
  nodejs \
  openssh-client rsync zip file bc \
  clang-23 clangd-23 lldb-23 lld-23 clang-format-23 clang-tidy-23 cmake ninja-build \
  libc++-23-dev libc++abi-23-dev liburing-dev pkg-config \
  llvm-23 \
  ccache strace patch \
  gcc-15 g++-15 gcc-16 g++-16 libstdc++-16-dev catch2 \
  && apt-get install -y --reinstall coreutils manpages manpages-dev \
  && apt-get clean && rm -rf /var/lib/apt/lists/*

# gcc 16.2 from the gcc-build stage. gcc adds no rpath, so binaries it links
# find its libstdc++ through ld.so.conf; the 000- name sorts ahead of the
# system paths so /opt/gcc-16/lib64 wins over the PPA snapshot's libstdc++.
COPY --from=gcc-build /opt/gcc-16 /opt/gcc-16
RUN echo /opt/gcc-16/lib64 > /etc/ld.so.conf.d/000-gcc-16.conf && ldconfig

# Alternatives for clang and gcc.
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-23 100 \
    --slave /usr/bin/clang++ clang++ /usr/bin/clang++-23 \
    --slave /usr/bin/clangd clangd /usr/bin/clangd-23 \
    --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-23 \
    --slave /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-23 \
    --slave /usr/bin/lldb lldb /usr/bin/lldb-23 && \
    update-alternatives --install /usr/bin/gcc gcc /opt/gcc-16/bin/gcc-16 100 \
    --slave /usr/bin/g++ g++ /opt/gcc-16/bin/g++-16

# The PPA snapshot's <format> gets one local patch, libstdcxx-format-clang.patch,
# so that clang can constant-evaluate format strings with a dynamic width or
# precision in C++26 mode (LLVM issue 73232 plus a static_assert clang rejects;
# details in the patch header and crossplatform.md). libstdcxx_format_check.cpp
# is compiled with clang++ against the patched header so the image build fails
# if the patch did not take. A patch that no longer applies after a snapshot
# bump means upstream changed the area: re-derive or retire the patch and the
# check together.
COPY libstdcxx-format-clang.patch libstdcxx_format_check.cpp /tmp/corvid/
RUN patch -p1 -d /usr/include/c++/16 < /tmp/corvid/libstdcxx-format-clang.patch && \
    clang++-23 -std=c++26 -fsyntax-only /tmp/corvid/libstdcxx_format_check.cpp && \
    rm -rf /tmp/corvid

# npm global dir writable by node.
RUN mkdir -p /usr/local/share/npm-global && \
  chown -R node:node /usr/local/share/npm-global

# Persist bash history.
RUN SNIPPET="export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" \
  && mkdir /commandhistory \
  && touch /commandhistory/.bash_history \
  && chown -R $USERNAME /commandhistory

ENV DEVCONTAINER=true

# Pre-create the memory bind-mount target so a fresh config volume is seeded with node-owned directories; otherwise Docker creates the intermediate dirs as root and Claude Code cannot write session transcripts.
RUN mkdir -p /workspace /home/node/.claude/projects/-workspace/memory && \
  chown -R node:node /workspace /home/node/.claude
WORKDIR /workspace

ARG GIT_DELTA_VERSION=0.18.2
RUN ARCH=$(dpkg --print-architecture) && \
  wget "https://github.com/dandavison/delta/releases/download/${GIT_DELTA_VERSION}/git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  dpkg -i "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb" && \
  rm "git-delta_${GIT_DELTA_VERSION}_${ARCH}.deb"

# SSH dir for the named volume (ownership seeds the volume on first mount).
RUN mkdir -p /home/node/.ssh && chown node:node /home/node/.ssh && chmod 700 /home/node/.ssh

USER node
ENV NPM_CONFIG_PREFIX=/usr/local/share/npm-global
ENV PATH=$PATH:/usr/local/share/npm-global/bin
ENV SHELL=/bin/zsh
ENV EDITOR=nano
ENV VISUAL=nano

# Default powerline10k theme.
ARG ZSH_IN_DOCKER_VERSION=1.2.0
RUN sh -c "$(wget -O- https://github.com/deluan/zsh-in-docker/releases/download/v${ZSH_IN_DOCKER_VERSION}/zsh-in-docker.sh)" -- \
  -p git \
  -p fzf \
  -a "source /usr/share/doc/fzf/examples/key-bindings.zsh" \
  -a "source /usr/share/doc/fzf/examples/completion.zsh" \
  -a "export PROMPT_COMMAND='history -a' && export HISTFILE=/commandhistory/.bash_history" \
  -x

RUN npm install -g @anthropic-ai/claude-code@${CLAUDE_CODE_VERSION}

# Firewall script and its sudo carve-out.
COPY init-firewall.sh /usr/local/bin/
USER root
RUN chmod +x /usr/local/bin/init-firewall.sh && \
  echo "node ALL=(root) NOPASSWD: /usr/local/bin/init-firewall.sh" > /etc/sudoers.d/node-firewall && \
  chmod 0440 /etc/sudoers.d/node-firewall

USER node

RUN git config --global url."git@github.com:stevensudit/".insteadOf "https://github.com/stevensudit/" && \
    git config --global include.path /home/node/node-dotfiles/.gitconfig-personal
RUN echo '[ -f /workspace/.devcontainer/shellrc.sh ] && source /workspace/.devcontainer/shellrc.sh' >> /home/node/.zshrc
