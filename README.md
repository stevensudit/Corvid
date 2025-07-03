# Corvid
<img src="https://upload.wikimedia.org/wikipedia/commons/0/0a/Corvus-brachyrhynchos-001.jpg" height=768 width=768>

Corvid: A general-purpose modern C++ library extending std.

https://github.com/stevensudit/Corvid

Copyright 2022-2025 Steven Sudit

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


HISTORY

This is a fork of the original Corvid library, which was written for C++17. It was initially called Corvid20, but now that it's moved past that, the version number has been removed.

With the C++17 version, rather than try to update it in place, it made more sense to make a new project and move code over, bit by bit, cleaning and upgrading it as I went. This approach enabled me to switch from MSVC to clang, which has resulted in more-compliant code, and to use GitHub Copilot and ChatGPT to help with the more tedious parts of the process. I also got rid of Google Test, just to simplify things, switching to the header-only Acutest, which is referenced but not bundled.


CONTENTS

The entirety of the Corvid library is in the `corvid` subdirectory, as headers you include into your own project. Everything else here, such as unit tests and various configuration settings, is just for my convenience in developing the library and is not properly a part of it. You're free to use it under the same license, though.


EXTERNAL DEPENDENCIES

LLVM suite: For clang, clang-format, and lldb. https://releases.llvm.org/download.html
CMake: For batch build files. https://cmake.org/download/
Acutest: For unit tests. https://github.com/mity/acutest


NOTICE

None of this code comes from any current or former employer. All work was done on my own time and equipment, either between jobs or with the knowledge and written consent of my employer at the time.
