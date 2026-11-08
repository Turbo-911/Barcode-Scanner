# Barcode-Scanner
A desktop application written in C++ that converts a six-digit number into a barcode image and reads a barcode back into its six-digit number, from either an image file or a camera. It is built without any third-party libraries.

## Features

- **Number to barcode:** enter a six-digit number and get a barcode image, previewed in the console and saved as a BMP file.
- **Barcode to number:** decode a barcode from a BMP image file.
- **Camera scanning:** capture frames from a webcam and decode the first barcode found.
- **Self-test:** an encode-then-decode round trip that checks the whole pipeline without needing files or a camera.

## How It Works

The project uses the **Interleaved 2 of 5 (ITF)** barcode format, which encodes digits in pairs. Six digits make exactly three pairs.

### Encoding rules

Every digit is made of five elements, each either narrow (N) or wide (W), with exactly two wide:

| Digit | Pattern   | Digit | Pattern   |
|-------|-----------|-------|-----------|
| 0     | N N W W N | 5     | W N W N N |
| 1     | W N N N W | 6     | N W W N N |
| 2     | N W N N W | 7     | N N N W W |
| 3     | W W N N N | 8     | W N N W N |
| 4     | N N W N W | 9     | N W N W N |

For each pair, the first digit is drawn in the bars and the second digit in the spaces, alternating bar, space, bar, space.

The full barcode layout is:

```text
[quiet zone] [START] [pair 1] [pair 2] [pair 3] [STOP] [quiet zone]
```

- **Start:** narrow bar, narrow space, narrow bar, narrow space
- **Stop:** wide bar, narrow space, narrow bar
- **Wide bar** is 3 times the width of a narrow bar
- **Quiet zone:** blank margin of at least 10 narrow widths on each side

### Number to barcode

1. Check the input is exactly six digits and starts with a non-zero digit.
2. Split it into three pairs.
3. Look up each digit's pattern and interleave the two patterns of each pair.
4. Add the start and stop patterns to get a list of bar and space widths.
5. Render the widths as black and white columns into an image and save it as a BMP.

### Barcode to number

1. Load the image and convert it to grayscale.
2. Convert it to pure black and white with a brightness threshold.
3. Take a horizontal scan line through the image, trying several rows for reliability.
4. Measure the lengths of consecutive black and white runs.
5. Use the four start-pattern runs to find the width of one narrow bar.
6. Classify every run as narrow or wide (wide if its width is at least twice the narrow width).
7. Remove the start and stop patterns and check that 30 elements remain.
8. Split the elements back into bar digits and space digits, and look each pattern up in the table.
9. If decoding fails, reverse the runs and try again to handle upside-down images.

## Project Structure

```text
barcode-scanner-cpp/
├── src/
│   ├── main.cpp          Program entry point
│   ├── App.h / App.cpp   Console menu and integration of all modules
│   ├── Common.h          Shared types and function declarations
│   ├── Encoder.h / .cpp  Number to bar widths, and barcode rendering
│   ├── Image.h / .cpp    Image class and grayscale handling
│   ├── BMP.h / .cpp      BMP file reader and writer
│   ├── Decoder.h / .cpp  Image to number
│   └── Camera.h / .cpp   Camera frame capture
├── tests/                Test images and test programs
├── README.md
└── .gitignore
```

## Usage

The program shows a menu:

```text
 1. Generate barcode from 6-digit number
 2. Read barcode from image file (.bmp)
 3. Scan barcode with camera
 4. Run self-test (encode -> decode)
 0. Exit
```

- **Option 1:** enter six digits. The barcode is previewed in the console and saved as `barcode_<number>.bmp`, or a file name of your choice.
- **Option 2:** enter the path to a BMP image. Paths wrapped in quotes (as when dragging a file into the console) are accepted.
- **Option 3:** hold a barcode in front of the camera. The program tries up to 30 frames. If none decode, the last frame is saved as `last_capture.bmp` for debugging.
- **Option 4:** runs a set of sample numbers through encode and decode and prints PASS or FAIL for each.

## Roles For Phase 1

| Member | Role | Responsibilities |
|--------|------|------------------|
| _Zainab_ | Integration | Repository, shared interfaces, `main.cpp`, `App.cpp`, README, merging |
| _Aliza_ | Encoder | Input validation, digit patterns, bar widths, barcode rendering |
| _Nabeera_ | Image I/O | Image class, BMP reader and writer, grayscale conversion, test images |
| _Arooj_ | Decoder | Thresholding, scan lines, run lengths, pattern lookup, upside-down handling |
| _Talha_ | Camera and Testing | Camera capture, test cases, bug reports |

## Known Limitations

- Only six-digit numbers starting with a non-zero digit are supported.
- Image files must be BMP.
- Heavily blurred, rotated, or low-contrast images may not decode.
- Camera support depends on the Windows camera APIs available on the machine.

## License

Developed as a university course project.
