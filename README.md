# Barcode-Scanner
A desktop application written in C++ for a small store: it converts a six-digit number into a barcode image, reads a barcode back into its six-digit number (from an image file or a camera), and runs a point of sale on top of that barcode core — products, stock, orders and bills. The Phase 1 core is built without third-party libraries; the Phase 2 GUI uses SFML 3. Phase 3 grows the POS into PoS v2: product categories, seasonal and special discounts, a split bill format, cashier/manager roles and manager reports.

## Features

### Phase 1 — Barcode core

- **Number to barcode:** enter a six-digit number and get a barcode image, previewed in the console and saved as a BMP file.
- **Barcode to number:** decode a barcode from a BMP image file.
- **Camera scanning:** capture frames from a webcam and decode the first barcode found.
- **Self-test:** an encode-then-decode round trip that checks the whole pipeline without needing files or a camera.

### Phase 2 — Point of Sale

- **Product catalogue:** products with price, tax, discount and live stock, stored in `Data/product.txt`.
- **Product description:** scan a product's barcode (camera or file) or type it to see its full specification; an unknown barcode gives a clear "no product found" message.
- **Scan-to-order:** scanning a product with the POS camera adds it to the current order automatically — scanning it again adds one more.
- **Stock rule:** a product is added only when the required quantity is within the available stock.
- **Checkout and billing:** the bill is calculated, shown on screen, written to the data files, and stock is reduced.

### Phase 3 — PoS v2

- **Product categories:** the manager configures categories in `Data/PCategory.txt`; every product carries a Category Id, and the first two digits of its barcode are the category.
- **Order editing:** orders can be held and resumed with minimum clicks; a paid order can be edited within 7 days of payment, only after manager approval.
- **Inventory:** the manager can view and update stock at any time, and add or edit any product.
- **Split bill format:** `Data/bill.txt` holds one line per bill (totals, amount received, balance returned, seasonal and special discounts); the per-product lines live in `Data/BillDetail.txt`.
- **Discounts:** seasonal discounts from manager-configured seasons in `Data/Seasons.txt` apply automatically at billing; a special discount of up to 8% can be given at checkout.
- **PDF bills:** a bill can be printed as a PDF by the cashier or the manager.
- **Roles:** cashier and manager features are provided separately through the UI.
- **Manager reports:** incomplete and completed orders with totals, cash sales (daily / weekly / monthly / yearly / custom period), all sales with discounts, top 10 most sold items and top 10 items by remaining stock.

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
Barcode-Scanner/
├── run_gui.bat           ONE command: builds everything and launches the GUI
├── CMakeLists.txt        Optional CMake build (same sources)
├── Data/                 Phase 2 + 3 data files (repository names)
│   ├── product.txt       Products: id barcode acronym name unit price tax discount qty barcodeFile categoryId #
│   ├── PCategory.txt     Categories: categoryId name acronym #
│   ├── order.txt         Orders: orderId date time customer contact status amount mode merchant #
│   ├── orderDetail.txt   Order lines: orderId productId productBarcode quantity #
│   ├── bill.txt          One line per bill: billId customer orderId totals payable received balance status seasonal special date time #
│   ├── BillDetail.txt    Bill lines: billId acronym unitPrice quantity lineDiscount #
│   └── Seasons.txt       Seasons: startDate endDate discountPercent isActive #
├── assets/fonts/         Fonts bundled for the GUI
├── barcodes/             Sample barcode images
├── src/
│   ├── main.cpp          Entry point: GUI by default, --console / --selftest / --screenshot
│   ├── App.h / App.cpp   Phase 1 console menu
│   ├── barcode/          ITF encoder/decoder, shared patterns, encoder/decoder interfaces
│   ├── camera/           Webcam capture (Media Foundation) + frame-source interface
│   ├── common/           common.h (shared types), BMP codec, image helpers, image_io,
│   │                     string_utils (file text rules), date_time (the only clock reader)
│   ├── domain/           Pure data: product.h, order.h (Order + OrderDetail), bill.h
│   │                     (Bill + BillDetail), enums (statuses / payment modes / roles)
│   ├── persistence/      record_file (only raw file IO), one serialiser per data file,
│   │                     repository interfaces + file repositories (products, orders,
│   │                     bills, bill details, categories, seasons)
│   ├── services/         id_generator, bill_calculator (money maths), bill_service,
│   │                     product_service (incl. THE stock rule), order_service,
│   │                     category_service, seasons_service, report_service,
│   │                     pdf_bill (hand-written PDF writer), scan_service
│   └── ui/               SFML GUI: BarcodeStudioGui, Widgets, Theme (presentation only)
├── tests/                Test barcode images, pos_selftest.cpp, seed_history.cpp
└── docs/DESIGN.md        Architecture, SE principles, format decisions
```

## Phase 2 — Point of Sale (Chili Mili)

On top of the Phase 1 barcode core, the GUI is a small store POS:

- **Products** are kept in `Data/product.txt`. Scanning a product's
  barcode (camera or file), typing it, or pressing **Description** in
  the POS screen shows its full specification; a barcode with no
  matching product shows a clear "no product found" message.
- **POS Order screen:** scanning a product with the POS camera adds it
  to the order automatically (one unit per scan — scanning again adds
  another, merged into the same line), and a decoded barcode on the
  Camera or File scanner screens can also be sent straight into the
  current order. Quantities can be set by hand as well — a product is
  added only when the required quantity is within the available stock
  (the stock rule lives in `ProductService::canSell`).
- **Customer and payment:** the customer's name and contact number
  (exactly 11 digits) are recorded with the order, and the payment
  mode, order status and merchant are picked from drop-down menus —
  the merchant list follows the payment mode (banks for card,
  JazzCash / EasyPaisa / Raast for wallet).
- **Generate Bill** writes the order to `Data/order.txt`, its lines to
  `Data/orderDetail.txt` and the bill to `Data/bill.txt`, shows the
  bill on screen, and reduces stock.
- **Products screen:** the full product list with quantities in stock,
  refreshed from `Data/product.txt`.
- The design follows SRP / OCP throughout — see `docs/DESIGN.md`.

## Phase 3 — PoS v2

- **Categories first:** the manager creates categories on the
  Categories screen; adding a product then picks a category, and its
  barcode is generated with the category as the first two digits.
- **Hold and resume:** an order can be held from the POS screen with
  one button and resumed later from the Held Orders screen; stock
  moves only when the order is finally billed.
- **Editing a paid order:** on the Order Edit screen the manager
  approves, the cashier adjusts the customer details and line
  quantities, and saving rewrites the order and re-issues its bill
  in place (same Bill Id), reconciling stock. Only paid orders, only
  within 7 days of payment.
- **Seasons and discounts:** the manager defines seasons (date range
  + discount %) and activates or deactivates them; at checkout an
  active season's discount is applied automatically, and a special
  discount of up to 8% can be added on top. Both amounts are recorded
  on the bill.
- **Amount received / balance:** checkout records what the customer
  handed over and the balance returned.
- **Reports:** the Reports screen runs the six manager reports in
  tabular form, including cash sales for a chosen period and the two
  top-10 lists.
- **Print Bill:** the bill screen can save the bill as a PDF
  (`bills/<Bill Id>.pdf`).

## Running

```text
.\run_gui.bat
```

That is the only step: it builds with MSYS2 UCRT64 g++ + SFML 3 and
launches the GUI. Other modes: `barcode_studio_gui.exe --console`
(Phase 1 console menu), `--selftest` (headless round-trip check).

## Usage

The GUI has screens for the barcode generator, the file scanner,
the camera scanner, POS orders, the product list and the self-test,
plus the Phase 3 screens: categories, seasons, inventory, product
edit, reports, held orders, order edit and bill print. A role button
switches between Cashier and Manager; the manager screens belong to
the Manager role.

The console program (`--console`) shows a menu:

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

## Testing

- `barcode_studio_gui.exe --selftest` — headless encode/decode
  round-trip check over a set of sample numbers.
- `tests/pos_selftest.cpp` — headless checks of the POS services:
  the stock rule, bill calculation, checkout file writes, and the
  Phase 3 flows (categories, seasonal discount, the 8% special cap,
  hold/resume, paid-order edit, reports, PDF output).
- The images in `tests/` cover normal, blurry, rotated, noisy,
  inverted and different-size barcodes, plus one barcode image per
  product in the catalogue.

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
- Order and Bill Ids are time-based (HHMMSS + day), so two checkouts
  within the same second would generate the same id.

## License

Copyright (c) 2026 Chili Mili (Zainab, Aliza, Nabeera, Arooj and Talha).
All rights reserved — see the [LICENSE](LICENSE) file. This project
may not be used, copied, modified, or distributed without the prior
written permission of the authors.

Developed as a university course project.
