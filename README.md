![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-zxing)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-zxing/total)

# 4d-plugin-zxing
Decode QR code with ZXing.

macOS: 

* [nu-book/zxing-cpp](https://github.com/nu-book/zxing-cpp/releases)

Windows: 

* [nu-book/zxing-cpp](https://github.com/nu-book/zxing-cpp/releases) : requires VS 2017
* [rootzzp/zxing-cpp-1](https://github.com/rootzzp/zxing-cpp-1) : old but compiles VS 2015
* [glassechidna/zxing-cpp](https://github.com/glassechidna/zxing-cpp) : even older; same as `vcpkg zxing-cpp`

#### Syntax

```4d
status:=zxing decode(image)
```
