// Synthetic ZXing boundary for the ownership harness (no decoding).
#pragma once
#include <string>
#include <vector>
#include <array>
#include <cstdint>
namespace ZXing {
enum class DecodeStatus { NoError = 0, NotFound, FormatError, ChecksumError };
enum class BarcodeFormat { None = 0, Aztec, Codabar, Code39, Code93, Code128, DataBar, DataBarExpanded,
    DataMatrix, EAN8, EAN13, ITF, MaxiCode, PDF417, QRCode, UPCA, UPCE };
enum class ImageFormat : uint32_t { None = 0, RGB = 3 };
template <typename T> struct PointT { T x = 0, y = 0; };
struct DecodeHints {};
class ImageView {
public:
    const uint8_t *_data; int _width, _height; ImageFormat _format;
    ImageView(const uint8_t *data, int w, int h, ImageFormat f, int = 0, int = 0)
        : _data(data), _width(w), _height(h), _format(f) {}
};
class Result {
public:
    DecodeStatus _status = DecodeStatus::NoError;
    BarcodeFormat _format = BarcodeFormat::QRCode;
    std::wstring _text, _ecLevel;
    std::string _symbologyIdentifier, _sequenceId;
    std::array<PointT<int>, 4> _position{};
    DecodeStatus status() const { return _status; }
    BarcodeFormat format() const { return _format; }
    const std::wstring &text() const { return _text; }
    const std::wstring &ecLevel() const { return _ecLevel; }
    const std::string &symbologyIdentifier() const { return _symbologyIdentifier; }
    const std::string &sequenceId() const { return _sequenceId; }
    const std::array<PointT<int>, 4> &position() const { return _position; }
    int orientation() const { return 0; }
    int sequenceSize() const { return -1; }
    int numBits() const { return 0; }
    int lineCount() const { return 0; }
    bool isLastInSequence() const { return true; }
    bool isMirrored() const { return false; }
    bool isPartOfSequence() const { return false; }
    bool isValid() const { return _status == DecodeStatus::NoError; }
};
using Results = std::vector<Result>;
Results ReadBarcodes(const ImageView &buffer, const DecodeHints &hints = {});
}
