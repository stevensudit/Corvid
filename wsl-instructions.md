1. Install WSL 2 with Noble Numbat, and do the usual `apt update` and `apt upgrade`.
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
sudo ./llvm.sh 22
sudo apt update
sudo apt install -y clang-22 lldb-22 lld-22 clang-format-22 clang-tidy-22 cmake ninja-build
sudo apt-get install -y libc++-22-dev libc++abi-22-dev liburing-dev pkg-config
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-22 100 \
--slave /usr/bin/clang++ clang++ /usr/bin/clang++-22
sudo apt install -y catch22 ccache strace
```
8. Install the latest GCC for its libstdc++.
```
sudo apt install -y software-properties-common
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install -y gcc-16 g++-16 libstdc++-16-dev
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-16 100 \
--slave /usr/bin/g++ g++ /usr/bin/g++-16
```
9. CUDA install. First install on Windows, then install inside WSL, but not
   with `sudo apt install cuda`!

   Windows:
   https://developer.nvidia.com/cuda-downloads?target_os=Windows&target_arch=x86_64&target_version=11&target_type=exe_network

   Linux WSL:
   https://developer.nvidia.com/cuda-downloads?target_os=Linux&target_arch=x86_64&Distribution=WSL-Ubuntu&target_version=2.0&target_type=deb_network

   You want `nvidia-smi` inside and outside WSL to show the same version.

   ```
   wget https://developer.download.nvidia.com/compute/cuda/repos/wsl-ubuntu/x86_64/cuda-keyring_1.1-1_all.deb
   sudo dpkg -i cuda-keyring_1.1-1_all.deb
   sudo apt-get update
   sudo apt-get -y install cuda-toolkit-13-3
   sudo apt install gcc-15 g++-15
   ```

   You will likely need to add `/usr/local/cuda-13.3/bin/` to your path in `.bashrc`.

10. In the repo, run `code .`, which brings up VSCode remotely.
11. Run `./cleanbuild.sh tidy` to build the tests with clang-tidy enabled. See usage for how to select compiler and standard library.
