# onnxruntime headers

The C and C++ API headers of [onnxruntime](https://github.com/microsoft/onnxruntime)
1.20.1 (MIT, see `LICENSE`), copied unchanged from the official release so the face
mask filter builds without onnxruntime installed. The library itself is not linked:
`ort-loader.cpp` opens it at runtime (`ORT_API_MANUAL_INIT`). Kept out of
clang-format by the `.clang-format` here.
