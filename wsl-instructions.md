1. Install WSL 2 with Jammy, and do the usual `apt update` and `apt upgrade`.
2. Install VSCode on Windows, including [WSL support](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-wsl).
3. All steps below are in WSL. First, install `gh` and link it to your account.
4. Clone the repo. Also, clone `mity/acutest`.
5. Run `git config –global user.name "user name"` and `git config –global user.email "user_name@gmail.com"`.
6. Install the clang key.
```
    curl -sS https://apt.llvm.org/llvm-apt-public-key.gpg -o llvm-apt-public-key.gpg
    sudo cp llvm-apt-public-key.gpg /etc/apt/trusted.gpg.d/apt.llvm.org.asc
    sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 15CF4D18AF4F7421
    sudo add-apt-repository -y "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-19 main"
```
7. Install clang and related tools.
```
  sudo apt -qq install \
    clang-19 \
    clang-format-19 \
    clang-tidy-19 \
    clang-tools-19 \
    clangd-19 \
    libc++-19-dev \
    libc++abi-19-dev \
    libclang-19-dev \
    libclang-common-19-dev \
    libclang-cpp19-dev \
    libclang-rt-19-dev \
    libomp-19-dev \
    libpolly-19-dev \
    libunwind-19-dev \
    lld-19 \
    lldb-19 \
    llvm-19 \
    llvm-19-dev \
    llvm-19-dev lld-19 \
    llvm-19-tools \
    cmake \
    ninja-build
```
8. In the repo, run `code .`, which brings up VSCode remotely.
