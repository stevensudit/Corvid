# corvid20
<img src="https://upload.wikimedia.org/wikipedia/commons/0/0a/Corvus-brachyrhynchos-001.jpg" height=768 width=768>

Corvid20: A general-purpose C++20 library extending std.

https://github.com/stevensudit/Corvid20

Copyright 2022-2024 Steven Sudit

Licensed under the Apache License, Version 2.0(the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


HISTORY

This is a fork of the original Corvid library, which didn't take advantage of C++20 features.

Rather than try to update it in place, it made more sense to make a new project and move code over, bit by bit, cleaning and upgrading it as I went. This approach enabled me to switch from MSVC to clang and gcc, which has resulted in more-compliant code, and to use GitHub Copilot and ChatGPT to help with the more tedious parts of the process. I also got rid of Google Test, just to simplify things, switching to the header-only Accutest, which is referenced but not bundled.


CONTENTS

The entirety of the Corvid library is in the `corvid` subdirectory, as headers you include into your own project. Everything else here, such as unit tests and various configuration settings, is just for my convenience in developing the library and is not properly a part of it. You're free to use it under the same license, though.


NOTICE

None of this code comes from my current or former employers. All work was done on my own time and equipment, either between jobs or with the knowledge and consent of my employer at the time.
