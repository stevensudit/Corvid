1. Install WSL 2 with Jammy, and do the usual `apt update` and `apt upgrade`.
2. Install VSCode on Windows, including [WSL support](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-wsl).
3. All steps below are in WSL. First, install `gh` and link it to your account.
```
sudo apt install gh
gh auth login
gh auth setup-git
gh auth status
```
5. Run `git config --global user.name "user name"` and `git config --global user.email "user_name@gmail.com"`.
6. Clone the repo.
7. Install clang.
```
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 21
sudo apt update
sudo apt install clang-21 lldb-21 lld-21 clang-format-21 cmake ninja-build
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-21 100 \
--slave /usr/bin/clang++ clang++ /usr/bin/clang++-21
```
8. In the repo, run `code .`, which brings up VSCode remotely.
9. Run `./cleanbuild.sh tidy` to build the tests with clang-tidy enabled. You can specify `libstdcpp` or `libcxx` as the first argument to choose the standard library.
