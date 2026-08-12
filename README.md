![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-zxing)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-zxing/total)

# 4d-plugin-zxing

Decodes 1D and 2D barcodes out of a `Picture` using the [ZXing](https://github.com/nu-book/zxing-cpp) (`zxing-cpp`) barcode-reading library. Internally the plugin converts the picture to BMP via 4D's own `CONVERT PICTURE` command, decodes the resulting bitmap through ZXing's `ReadBarcodes`, and returns everything ZXing found as a single `Object`.

**Platforms:** macOS (Intel & Apple Silicon), Windows 64-bit — requires 4D v18 or later.

| Command | Returns | Purpose |
|---|---|---|
| [`zxing decode`](#zxing-decode) | Object | Decode all barcodes found in a picture |

---

## Requirements & platform notes

- The command's single parameter is **mandatory** — there is no optional/overloaded form.
- Decoding failures are **not** reported as a 4D error. `zxing decode` always returns an `Object`; an image with no readable barcode simply comes back with an empty `results` collection. Check `results.length`, or each result's own `status` field, rather than wrapping the call in `ON ERR CALL`.
- **As of this build**, any unexpected failure during decoding (a corrupt/adversarial image that the underlying library can't parse, an out-of-memory condition, etc.) is caught internally and returned as `{status: "error", results: []}` instead of leaving the call unanswered. This behavior — and the `"error"`/`"unknown"` values described below — is true of this fixed source; it is not necessarily true of every previously-built binary of this plugin, since earlier builds could leave a call unanswered on an internal exception.
- **As of this build**, the plugin also disposes both internal `Picture` handles it creates per call (the duplicated input picture and the intermediate BMP conversion) rather than leaking them. This has no effect on the command's interface or return value — it's a memory-management fix, not a behavior change you'd observe from 4D code — but it's the difference between this build and earlier ones under sustained/high-volume scanning.
- There is no behavioral difference between macOS and Windows for this command — both platforms go through the same cross-platform decode path (`CONVERT PICTURE` → BMP → ZXing), so nothing below is platform-specific.
- Pictures backed by more than ~2 GB of image data are rejected internally rather than decoded (no result is produced for that call, rather than a crash or truncated read). This is a theoretical edge case for ordinary barcode photos, not something you'd hit with typical scan images.

---

## zxing decode

### Syntax

```4d
status:=zxing decode(image)
```

| Parameter | Type | Description |
|---|---|---|
| `image` | Picture | The picture to scan for barcodes. |
| Result | Object | See [Description](#description) below for the full shape. |

### Description

`zxing decode` scans `image` for every barcode ZXing can find and returns one `Object`:

| Property | Type | Description |
|---|---|---|
| `results` | Collection | One element per barcode found. Empty if none were found — always present. |
| `status` | Text | Only present, and only ever `"error"`, if this build caught an unexpected internal failure while decoding (see below). Absent on a normal call, including one that simply found no barcode. |

Each element of `results` is itself an `Object`:

| Property | Type | Description |
|---|---|---|
| `status` | Text | One of `"noError"`, `"notFound"`, `"formatError"`, `"checksumError"`, or `"unknown"` (see below). |
| `text` | Text | The decoded payload of the barcode. |
| `symbologyIdentifier` | Text | The AIM symbology identifier ZXing reports for this result. |
| `ecLevel` | Text | Error-correction level, where the format has one (e.g. QR Code); empty for formats that don't. |
| `sequenceId` | Text | Sequence identifier, for symbologies that split data across multiple symbols (e.g. structured-append QR Codes). |
| `orientation` | Number | Detected rotation of the symbol, in degrees. |
| `sequenceSize` | Number | Total number of symbols in the sequence this result belongs to. |
| `numBits` | Number | Number of raw bits decoded. |
| `format` | Text | The barcode symbology. One of `"none"`, `"aztec"`, `"codabar"`, `"code39"`, `"code93"`, `"code128"`, `"dataBar"`, `"dataBarExpanded"`, `"dataMatrix"`, `"EAN8"`, `"EAN13"`, `"ITF"`, `"maxiCode"`, `"PDF417"`, `"QRCode"`, `"UPCA"`, `"UPCE"`, or `"unknown"` (see below). |
| `isLastInSequence` | Boolean | `True` if this is the last symbol of a multi-symbol sequence. |
| `isMirrored` | Boolean | `True` if the symbol was detected mirrored. |
| `isPartOfSequence` | Boolean | `True` if this symbol is part of a multi-symbol sequence. |
| `isValid` | Boolean | `True` if the result passed the format's own validity checks. |
| `lineCount` | Number | Number of scan lines that contributed to this result (relevant to 1D symbologies). |
| `corners` | Collection | The symbol's corner points, as `{x, y}` objects — see below. |

`corners` holds one `{x: Number, y: Number}` object per point ZXing reports for that symbol's bounding quadrilateral. For most 2D symbologies this is four points (the four corners); the exact count comes from the ZXing library itself for the symbology in question and isn't something this plugin fixes or constrains, so don't hard-code an assumption of exactly four elements.

**`status: "unknown"` and `format: "unknown"`** are fallback values this build reports if ZXing ever returns a status or format value not in the lists above (for example, after a future ZXing upgrade adds a new symbology). They mean "recognized by the library, not yet named by this plugin" rather than a decode failure — a genuine failure to read the barcode is `status: "notFound"`, not `"unknown"`.

A top-level **`status: "error"`** (distinct from the per-result `status` field described above, and only ever added to the outer object, never to an element of `results`) means the decode attempt itself failed unexpectedly — as opposed to a per-result `status: "notFound"`, which is a normal ZXing outcome meaning "no barcode there." Test for this with `$status.status="error"` if you need to tell "genuine failure" apart from "no barcode present" (both otherwise leave `results` empty). In practice you'll only see this on corrupt input the underlying decoder can't process at all.

### Example

From the plugin's own test method (`TEST.4dm`):

```4d
//%attributes = {}
$file:=Folder:C1567(fk desktop folder:K87:19).file("調整後.jpg")

READ PICTURE FILE:C678($file.platformPath; $image)

$status:=zxing decode($image)
```

Reading every decoded value out of the result:

```4d
$status:=zxing decode($image)

If ($status.results.length>0)
	For each ($result; $status.results)
		ALERT($result.format+": "+$result.text)
	End for each
Else
	ALERT("No barcode found")
End if
```

A generic dispatcher on the per-result `status`, mirroring the shape ZXing itself returns:

```4d
$status:=zxing decode($image)

For each ($result; $status.results)
	Case of
		: ($result.status="noError")
			ALERT("Decoded ("+$result.format+"): "+$result.text)
		: ($result.status="notFound")
			ALERT("No barcode in this region")
		: ($result.status="formatError")
			ALERT("Malformed barcode data")
		: ($result.status="checksumError")
			ALERT("Checksum failed — likely a damaged/low-quality scan")
		Else
			ALERT("Unrecognized status: "+$result.status)
	End case
End for each
```

---

## Error handling & troubleshooting

- **No result found is not a 4D error.** `zxing decode` always returns an `Object`; check `results.length` (or lack of any element with `status: "noError"`) to detect "nothing decodable in this image," rather than expecting an exception.
- **An empty `results` collection can mean two different things.** It means either "no barcode present" (the normal case) or "the decode attempt itself failed" (this build's `status: "error"` case). Check `$status.status="error"` first if you need to tell them apart — don't assume an empty collection always means "no barcode."
- **`"unknown"` values mean the plugin is behind the library, not that decoding failed.** If you see `status: "unknown"` or `format: "unknown"` on a real result, ZXing successfully decoded something the plugin's format/status list doesn't yet name — treat it as a successful decode with an unrecognized label, not as an error.
- **Very large source images silently produce no result for that call.** There's no explicit warning for this — if you're scanning very high-resolution pictures and get an empty `results` collection unexpectedly, check the picture's size before assuming there's simply no barcode present.
- **`ecLevel` and `sequenceId` are often empty strings**, not omitted — they only carry a value for symbologies that support error-correction levels or multi-symbol sequences respectively. An empty string for these fields on a QR Code result, for instance, is unusual and worth double-checking against the source image; an empty string on a Code 128 result is expected.

---

## Quick reference

```4d
$status:=zxing decode($image)
If ($status.results.length>0)
	For each ($result; $status.results)
		ALERT($result.format+": "+$result.text)
	End for each
End if
```
