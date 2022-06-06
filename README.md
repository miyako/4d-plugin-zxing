
# 4d-plugin-zxing
Decode QR code with ZXing.

macOS: 

* [nu-book/zxing-cpp](https://github.com/nu-book/zxing-cpp/releases)

Windows: 

* [nu-book/zxing-cpp](https://github.com/nu-book/zxing-cpp/releases) : VS 2017
* [rootzzp/zxing-cpp-1](https://github.com/rootzzp/zxing-cpp-1) : old but compiles VS 2015
* [glassechidna/zxing-cpp](https://github.com/glassechidna/zxing-cpp) : even older; same as `vcpkg zxing-cpp`

#### Syntax

```4d
status:=zxing decode(image)
```
