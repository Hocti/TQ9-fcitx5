# 九万輸入法 Fcitx (Q9Fcitx)

## Introduction

**九万輸入法 Fcitx** is a specialized Chinese Input Method Engine (IME) developed for the Linux platform, specifically tailored for the **Fcitx5** framework. This project represents a comprehensive port of the Windows-based portable IME, [Q9CS](https://github.com/Hocti/Q9CS), adapting its functionality for contemporary Linux environments.

The core input logic is based on the traditional **Q9** methodology, which resides in the public domain following the expiration of patent **HK1035043**.

## Intellectual Property & AI-Assisted Development

The source code for this repository was predominantly synthesized with the assistance of advanced Large Language Models, specifically **Gemini** and **Claude**.

**Important Notice regarding Datasets:**
Out of an abundance of caution regarding potential copyright complexities—particularly concerning specific code tables and linguistic datasets—this repository **does not currently include any code packs**. Although the underlying patent has expired, the proprietary nature of certain data sets remains a subject of caution. Users are expected to provide their own data or refer to the original repository for data acquisition strategies.

## Installation Procedure

To facilitate the deployment of this IME on your system, please adhere to the following steps:

1. **Download:** Acquisition of the latest archive can be performed via the [Releases](https://github.com/Hocti/Q9-Linux/releases) page.
2. **Extraction:** Decompress the downloaded archive.
3. **Execution:** Navigate to the extracted directory and run the installation script:
   ```bash
   chmod +x install.sh
   ./install.sh
   ```
4. **Activation:** Once the installation is finalized, restart the Fcitx5 daemon to apply changes:
   ```bash
   fcitx5 -r
   ```
   Finally, append the "TQ9" input method to your active configuration through the Fcitx5 configuration tool.

### Troubleshooting

**The floating UI never appears and the log shows `error due to GNU_PROPERTY_1_NEEDED_INDIRECT_EXTERN_ACCESS`**

```
warning: copy relocation against non-copyable protected symbol
  `_ZN15QSocketNotifier16staticMetaObjectE' in `/usr/lib/libQt6Core.so.6'
fcitx5-tq9-ui: _ZN15QSocketNotifier16staticMetaObjectE: /usr/lib/libQt6Core.so.6:
  error due to GNU_PROPERTY_1_NEEDED_INDIRECT_EXTERN_ACCESS
```

Distributions such as Arch build Qt6 with indirect external access, and a
`fcitx5-tq9-ui` compiled without the matching flag cannot be loaded against it.
Releases from 2026-08-27 onwards are built with `-mno-direct-extern-access` and
run on both kinds of system; upgrade to the latest archive. When building from
source, simply reconfigure the project — CMake applies the flag automatically:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
readelf -r build/fcitx5-tq9-ui | grep -c R_X86_64_COPY   # must print 0
```

## Operational Instructions

The input mechanics, shortcut configurations, and user interface paradigms are designed to remain consistent with the original `Q9CS` implementation. For comprehensive documentation regarding keystroke mappings, Numpad optimization, and advanced customization, please consult the [Original Q9CS Documentation](https://github.com/Hocti/Q9CS#readme).

### Core Features

- **Optimized Numpad Input:** Highly efficient character entry leveraging a 3x3 numeric grid.
- **Intelligent Word Association:** Facilitates rapid selection of related characters to boost productivity.
- **Seamless Homophone Navigation:** Enables quick transitions to homophone selection when required.
- **Linguistic Versatility:** Native support for both Traditional and Simplified Chinese output.

## License

This project is licensed under the [MIT License](LICENSE.md).
