# Design Notes — Chili Mili Barcode POS (Phase 1 + Phase 2)

Group: **Chili Mili**

> **Repository structure (GitHub layout).** The code now follows the
> folder structure of the GitHub repository: `src` is split into
> `barcode/`, `camera/`, `common/`, `domain/`, `persistence/`,
> `services/` and `ui/`, the data files live in `Data/` with the
> repository's lower-case names (`product.txt`, `order.txt`,
> `orderDetail.txt`, `bill.txt`), and every formerly empty repository
> file is implemented. **No functionality changed** — the same classes,
> the same logic, the same file formats; code only moved to its
> structured home, and a few pieces that used to be private copies
> inside one file now live in the shared file the structure provides:
>
> | Was (private / duplicated) | Now (one shared home) |
> |---|---|
> | ITF digit table copied in encoder.cpp AND Decoder.cpp | `src/barcode/itf_patterns.h` |
> | Text helpers in `persistence/TextTokens.h` | `src/common/string_utils.h/.cpp` |
> | Clock/strftime code private to IdGenerator | `src/common/date_time.h/.cpp` |
> | Status/mode lists private to the GUI | `src/domain/enums.h/.cpp` |
> | Bill-row building inside OrderService::checkout | `src/services/bill_service.h/.cpp` |
> | Camera consistency rule inside App's menu code | `src/services/scan_service.h/.cpp` |
> | Direct calls only, no contracts for barcode/camera | `src/barcode/i_barcode_encoder.h`, `i_barcode_decoder.h`, `src/camera/i_frame_source.h` (interfaces + adapters over the original free functions) |
>
> Files the repository structure was missing, added in its own naming
> style: `src/persistence/i_order_repo.h/.cpp`, `i_bill_repo.cpp`,
> `bill_serialiser.h/.cpp`, and the whole `src/ui/` folder (the
> repository had no GUI folder, but Phase 2 is GUI-based) plus
> `assets/fonts/`, `run_gui.bat` and `CMakeLists.txt`.

## Layers and who is responsible for what (SRP)

| File / class | Its ONE responsibility |
|---|---|
| `src/barcode/encoder.cpp`, `src/barcode/Decoder.cpp`, `src/common/BMP.cpp`, `src/common/image.cpp`, `src/camera/camera.cpp` | Phase 1 barcode core (unchanged from Increment 1): ITF encode/decode, BMP IO, webcam frames |
| `src/domain/product.h`, `order.h` (Order + OrderDetail), `bill.h`, `enums.h/.cpp` | Pure data shapes that mirror one file row each. No logic |
| `src/persistence/record_file` | The only class that reads/writes raw text lines on disk |
| `src/persistence/product_serialiser` / `order_serialiser` / `order_detail_serialiser` / `bill_serialiser` | Convert ONE entity <-> ONE file line, in the exact field order of the requirements |
| `ProductFileRepository` (declared in `i_product_repo.h`, defined in `i_product_repo.cpp`), `OrderFileRepository`, `OrderDetailFileRepository` (in `i_order_repo.h/.cpp`), `BillFileRepository` (in `i_bill_repo.h/.cpp`) | Storage of one file each, built from RecordFile + its serialiser |
| `src/persistence/i_product_repo.h`, `i_order_repo.h`, `i_bill_repo.h` | Storage **interfaces**. Services depend on these, never on file classes |
| `src/services/id_generator` (clock reading: `src/common/date_time`) | How Order Ids (8 digits, last two = day of the month), Bill Ids (9 digits), dates and times are formed |
| `src/services/bill_calculator` | Pure money maths: totals without tax, tax, discount, payable. No files, no GUI — unit-testable |
| `src/services/product_service` | Product lookups (description feature), THE stock rule, stock decrease after a sale |
| `src/services/order_service` (bill rows: `src/services/bill_service`) | Recording an order (cart) and checkout: writes Order + OrderDetails + Bill rows, updates stock |
| `src/ui/*` | Presentation only. The GUI calls the Phase 1 functions and the Phase 2 services; it contains no file-format or pricing logic |

## How OCP / AoC shows up

- **New storage without edits:** a future `ProductDbRepository` only has
  to implement `IProductRepository`. `ProductService`, `OrderService`
  and the GUI do not change (they are *closed* for modification, the
  system is *open* for extension).
- **New front-end without edits:** the console mode (`--console`), the
  SFML GUI and `tests/pos_selftest.cpp` all reuse the same services.
- **New screens without edits:** the POS and Products screens reuse the
  Phase 1 widgets (`PinkButton`, `PinkTextBox`, `ToastNotification`,
  theme, mascot) — nothing was forked or duplicated.
- **One rule, one place:** the stock rule lives only in
  `ProductService::canSell`, the money maths only in `BillCalculator`,
  the id scheme only in `IdGenerator`, the file formats only in the
  serialisers. Changing any rule means editing exactly one class.

## File formats (as specified)

- `Data/product.txt`: `id barcode acronym name unit price tax discount qty barcodeFile #`
- `Data/order.txt`: `orderId date time customerName contact status paymentAmount paymentMode merchant #`
  — Order Id is 8 digits and its last two digits are the day of the
  month of the order (Id = `HHMMSS` + `DD`).
- `Data/orderDetail.txt`: `orderId productId productBarcode quantity #`
- `Data/bill.txt`: one line **per ordered product**, all lines of a bill
  sharing the same 9-digit Bill Id and the same order-level totals:
  `billId customerName orderId acronym unitPrice discountPerUnit quantity totalWithoutTax totalTax totalDiscount totalPayable paymentStatus #`
- Bill Id is 9 digits (`1` + `HHMMSS` + `DD`).

Totals: `totalPayable = totalWithoutTax + totalTax - totalDiscount`,
where each total sums `per-unit value x quantity` over the order.

## Decisions where the spec leaves room (documented, one-line changes)

1. **Spaces inside a field** are impossible in a space-separated file,
   so names are stored with `_` instead of spaces (`Fresh_Milk`,
   `Ayesha_Khan`) and shown with spaces again in the GUI. A missing
   optional contact/merchant is stored as `-`. (See
   `common/string_utils` — the only place this rule lives.)
2. **Stock rule:** "the product shall be added if the required quantity
   is less than the available quantity" is implemented as *must not
   exceed* the available quantity (`quantity <= availableQuantity`), so
   the last unit in stock can still be sold. The check also applies to
   the combined quantity when the same product is added twice, and is
   re-checked at checkout. Stock in `Data/product.txt` is decreased after a
   bill is generated. Change `ProductService::canSell` to alter this.
3. **Camera scanning in the POS:** a scan fills the barcode box and
   shows the product description; the cashier sets the quantity and
   presses **Add to Order** (so quantity/stock are always confirmed).
   The **Description** button shows the same description for a typed
   barcode, as required for the cashier; scanning in the Camera screen
   also shows the product when one exists (manager/cashier view).

## Responsive layout

The UI is designed on a fixed 1000x680 canvas and `applyView()` scales
it (letterboxed, aspect preserved) to any window size — maximised or
fullscreen (**F11**) — so the interface fills the screen instead of
staying in one corner. Mouse coordinates go through the same view, so
clicks stay pixel-exact at every size.

## Tests

- `--selftest` — Phase 1 encode/decode round trip (6/6).
- `tests/pos_selftest.cpp` — 21 headless checks of the Phase 2 modules:
  Data/product.txt parsing, the stock rule, merged-quantity rejection, totals
  (480 + 25 - 10 = 495), id formats, all four files' round-trips, and
  the stock decrease after a sale.

## Mid-running sample data (v5)

The project ships as a store that is already operating:

- `Data/product.txt` — 16 products (count + weight units, varied tax and
  discount, one low-stock item at 3, one out-of-stock item at 0 so the
  stock rule can be demonstrated). Every product's barcode BMP exists
  in `tests/` and decodes back to its barcode.
- `Data/order.txt` / `Data/orderDetail.txt` / `Data/bill.txt` — past orders
  (2026-11-14 to 2026-11-18) with mixed statuses (paid, on-hold,
  in-progress) and payment modes (cash, card/ABL, wallet/JazzCash,
  wallet/EasyPaisa). Every Order Id ends with the day of its date and
  every bill's totals reconcile with `BillCalculator`.
- The history was produced by `tests/seed_history.cpp`, a small tool
  that writes through the project's own repositories, serialisers and
  `BillCalculator` — the same code the GUI uses — so the sample data
  can never drift from the real formats. Re-running it appends another
  copy of the 5 orders; delete the three files first for a clean seed.

## Maintenance review (v5)

Checked while adding the data, and fixed where found:

- `OrderService::checkout` now verifies **every** append (order, all
  details, all bill rows) and reports `write-failed` if any write did
  not reach disk, instead of trusting them silently.
- The payment-mode and order-status lists existed in two places (the
  click handler and checkout). They now live in exactly one shared
  table used by both (DRY — a SRP/quality fix).
- The Products screen paginates (8 per page, Prev/Next) so the list
  stays usable as the catalogue grows — the screen was extended, the
  widgets and services were not modified (OCP).
- Known limitation, accepted for this scope: Order/Bill Ids are
  time-based (`HHMMSS` + day), so two bills generated within the same
  second would share an Id. The single home for the scheme is
  `IdGenerator`, which is where a sequence suffix would be added.

## GUI polish (user feedback round)

- **Single screen, closable sidebar:** the sidebar is rendered to its
  own texture and slides away with the hamburger button; the content
  then centres on the one screen. No slide/fade screen transitions,
  no subtitles under headings, no numbering on the sidebar items.
- **Camera screen kept simple:** Start/Stop + Scan Another, the live
  feed, one status line, and the decoded output card. (The 3-in-a-row
  consistency rule still runs inside the worker - it is just not
  visualised anymore.)
- **Generated barcodes** always save into the project's `barcodes/`
  folder, and the Browse dialog opens in that same folder; `tests/`
  stays a separate test-images folder.
- **Products refresh fixed:** repositories now also look for `Data/`
  in the parent folder (running from a `build/` subfolder used to
  show an empty list), and every refresh re-locates the files.
- **POS payment controls are drop-downs** (`ui::PinkDropdown`):
  payment mode, order status, and merchant - the merchant list
  follows the mode (banks for card, JazzCash/EasyPaisa/Raast for
  wallet, none for cash).
- Font is Times New Roman throughout; the logo heart is drawn from a
  true parametric heart curve.

## GUI polish round 3 (user feedback)

- **Crisp text at any window size:** the layout now fills the whole
  window (layout width grows past 1000 units, right-hand panels
  anchor to the edge - no letterbox bars, nothing floats on a pink
  background). The sidebar/content textures render at the window's
  native pixel resolution, and all text is rasterised at its
  on-screen size (`theme::uiScale`, set in `applyView()`) - the old
  magnification blur is gone.
- **Products scrolls** with the mouse wheel (Prev/Next page through
  it too), with a slim scrollbar on the card's edge.
- **Validations audited:** generator code (6 digits, first non-zero),
  POS barcode (6 digits), quantity (1..stock, merged lines checked),
  customer name required, mode/status/merchant constrained by the
  drop-downs, and the contact number must be exactly 11 digits -
  the rule lives in `common/string_utils::isValidPhone`, the contact
  box is digits-only with max length 11, and checkout refuses
  anything else. Covered by 3 new checks in `tests/pos_selftest.cpp`.
- The corner toast box is gone (feedback stays in the screens and
  the mascot), and generated/decoded barcodes appear instantly -
  the reveal/sweep animation was removed.

## POS live camera view

The camera worker always decoded frames on the POS screen (a scan
fills the barcode box), but the POS page never DREW the feed. Now,
while the camera runs, the POS top-right card is a **Live Camera**
card: the live feed with a LIVE pill and a "Last scanned" line; when
the camera stops, the card returns to the product description.

## Unknown-barcode feedback

A successfully decoded barcode that has no product in
Data/product.txt used to show nothing on the Camera page. Now every
scan surface answers: Camera and File pages show "No product found
for barcode X in Data/product.txt" in the error colour (and the
mascot no longer celebrates an unknown code as found), and the POS
live-camera card shows "Scanned X - no product found" under the
feed; the POS entry message already named the missing barcode.

## Logo alignment + decoration cleanup

The sidebar heart is now centred on the "Chili Mili" title's real
text bounds (same fix on the bill header), and the ambient floating
hearts/sparkles plus the mouse sparkle trail were removed entirely -
they drifted behind headings and text. Short celebration bursts on
successful actions remain.

## Bigger text + roomier POS right column

All text is scaled up in one place (`theme::kTextBoost` = 1.15 in
`scaledSize`), so every screen, widget and pill grows together.
The POS right column (description / customer & payment / totals)
is wider (244 -> 284) and starts further left; the entry and cart
cards narrowed to match, with the cart quantity cluster and the
Start Cam button re-anchored, and the customer inputs widened.

## Camera scan after an order (v16 fix)

After the first order, camera scanning could stop responding: the
worker's half-built candidate and the scan lock were never reset
at order boundaries. There is now one `resetScanState()` (unlock,
drop any pending code, bump a generation counter the worker watches
to drop its candidate), called on camera start, Scan Another, bill
close and New Order; bill close and New Order also restart a running
camera so each order gets a completely fresh session.

## Fewer presses: scan-to-order flow (v17)

One barcode used to be scanned twice - once on the Camera/File
screens just to decode it, then again in POS to buy it - and every
POS scan still needed a separate Add to Order press. Three changes
remove the repeated work:

- POS camera scans add the item directly (`posAddOne`): one scan =
  one unit in the cart, scanning the same item again adds another,
  like a supermarket till. Typing a barcode still uses the qty box
  and Add to Order as before, and OrderService enforces stock for
  both paths.
- The Camera Scan and Read from File screens show an Add to Order
  button when the decoded barcode belongs to a product; one click
  adds it and lands on the POS screen (`addDecodedToOrder`), so a
  decode never has to be repeated as a POS scan.
- The Camera and POS screens share one camera session: switching
  between them no longer stops the camera (only the scan state is
  reset), so there is no Start Cam press in the middle of the flow.
  Leaving for any other screen still stops the camera.
