# Design Notes — Chili Mili Barcode POS (Phases 1 + 2 + 3)

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
| `src/persistence/record_file` | The only *production* class that reads/writes raw text lines on disk. Two exceptions, both named so nobody is misled: `tests/pos_selftest.cpp` seeds its own throwaway `Data/` with plain `ofstream` (test-only, and it refuses to run against a real catalogue), and binary IO lives elsewhere (`BMP.cpp` for images, `pdf_bill.cpp` for PDFs, `App.cpp` for `last_capture.bmp`) |
| `src/persistence/product_serialiser` / `order_serialiser` / `order_detail_serialiser` / `bill_serialiser` | Convert ONE entity <-> ONE file line, in the exact field order of the requirements |
| `ProductFileRepository` (declared in `i_product_repo.h`, defined in `i_product_repo.cpp`), `OrderFileRepository`, `OrderDetailFileRepository` (in `i_order_repo.h/.cpp`), `BillFileRepository` (in `i_bill_repo.h/.cpp`) | Storage of one file each, built from RecordFile + its serialiser |
| `src/persistence/i_product_repo.h`, `i_order_repo.h`, `i_bill_repo.h` | Storage **interfaces**. Services depend on these, never on file classes |
| `src/services/id_generator` (clock reading: `src/common/date_time`) | How Order Ids (8 digits, last two = day of the month), Bill Ids (9 digits), dates and times are formed |
| `src/services/bill_calculator` | Pure money maths: totals without tax, tax, discount, payable. No files, no GUI — unit-testable |
| `src/services/product_service` | Product lookups (description feature), THE stock rule, stock decrease after a sale |
| `src/services/order_service` (bill rows: `src/services/bill_service`) | Recording an order (cart) and checkout: writes Order + OrderDetails + Bill rows, updates stock |
| `src/ui/*` | Presentation plus display formatting. The GUI calls the Phase 1 functions and the Phase 2/3 services and does not write data files itself (since v35, even held-order delete goes through `OrderService::archiveOrder`). It DOES own presentation-side rules the services do not: the merchant drop-down lists, the phone-number formatting rule, and rejecting a special discount outside 0–8% before checkout. Those are UI rules, not file-format or money logic — but "no logic in the UI" would be false |

## How OCP / AoC shows up

- **New storage without edits:** a future `ProductDbRepository` only has
  to implement `IProductRepository`. `ProductService`, `OrderService`
  and the GUI do not change (they are *closed* for modification, the
  system is *open* for extension).
- **New front-end without edits:** the SFML GUI and
  `tests/pos_selftest.cpp` reuse the same services. The console mode
  (`--console`) does NOT — it is Phase 1 only (encoder/decoder/camera
  through `ScanService`), with no services, no products and no bills.
- **New screens without edits:** the POS and Products screens reuse the
  Phase 1 widgets (`PinkButton`, `PinkTextBox`, `PinkDropdown`, theme)
  — nothing was forked or duplicated. `ToastNotification` and the
  mascot are NOT reused: the toast class was deleted in v35 (it was
  dead code — `showToast()` had an empty body) and the mascot was
  removed at the user's request. Feedback is per-screen message lines
  plus a 3-message history on the POS screen.
- **One rule, one place — with the honest exceptions:** the stock rule
  is stated in `ProductService::canSell` and enforced by the single
  stock-adjust function `ProductService::modifyStock` (used by
  `decreaseStock`/`increaseStock`/`setStock`); the GUI Order Edit
  screen additionally caps a line quantity at the stock on hand, so
  the cap appears there too. The money maths lives only in
  `BillCalculator` — `BillService::buildBill` builds the bill row
  from its totals, and checkout/hold/edit all route through it.
  The id scheme lives only in `IdGenerator`, the file formats only in
  the serialisers. Payment-mode/status NAMES live in exactly one
  shared table (`src/domain/enums.h/.cpp`); the merchant allow-lists
  do not — they exist in the GUI drop-downs AND in
  `OrderService::validatePayment`, and the two must agree.

## File formats (as specified)

- `Data/product.txt`: `id barcode acronym name unit price tax discount qty barcodeFile #`
- `Data/order.txt`: `orderId date time customerName contact status paymentAmount paymentMode merchant #`
  — Order Id is 8 digits and its last two digits are the day of the
  month of the order (Id = `HHMMSS` + `DD`).
- `Data/orderDetail.txt`: `orderId productId productBarcode quantity #`
- `Data/bill.txt`: ONE line per bill, 14 fields + `#` (bill-level
  totals — the Phase 2 per-product bill format is retired, see the
  Phase 3 section): `billId customerName orderId totalWithoutTax
  totalTax totalDiscount totalPayable amountReceived balanceReturned
  paymentStatus seasonalDiscount specialDiscount billDate billTime #`
- `Data/BillDetail.txt`: one line per product on a bill, 5 fields + `#`:
  `billId productAcronym unitPriceWithoutTax quantity lineDiscount #`
  (line discount = per-unit discount x quantity). It carries no
  per-line tax or per-line total, so a bill can only be reconstructed
  by joining back to `product.txt` — a price change after the sale
  makes the historical bill unreconstructable from bill files alone.
- Bill Id is 9 digits (`1` + `HHMMSS` + `DD`).

Totals: `totalPayable = totalWithoutTax + totalTax - totalDiscount`,
where each total sums `per-unit value x quantity` over the order.

## Decisions where the spec leaves room (documented, one-line changes)

1. **Spaces inside a field** are impossible in a space-separated file,
   so names are stored with `_` instead of spaces (`Fresh_Milk`,
   `Ayesha_Khan`) and shown with spaces again in the GUI. A missing
   optional contact/merchant is stored as `-`. (See
   `common/string_utils` — the only place this rule lives.) This
   encoding is LOSSY, and that is a documented decision, not an
   oversight: a real `_` in a name (e.g. `Sugar_Free`) round-trips to
   `Sugar Free`, and a literal `-` name/contact/merchant is
   indistinguishable from missing. Acronym, unit, barcodeFile, status
   and payment mode are never encoded — a space in any of them shifts
   every later field and the line is dropped (and counted, v35).
2. **Stock rule (D-15 — deliberate deviation from the spec):** "the
   product shall be added if the required quantity is less than the
   available quantity" is implemented as *must not exceed* the
   available quantity (`quantity <= availableQuantity`), so
   the last unit in stock can still be sold. Buying the last unit is
   standard retail; the spec's strict `<` would leave one unit of
   everything permanently unsellable. Code, test and this document
   agree on `<=`. The check also applies to the combined quantity when the same product is added twice, and is
   re-checked at checkout. Stock in `Data/product.txt` is decreased after a
   bill is generated. Change `ProductService::canSell` to alter this.
3. **Camera scanning in the POS:** a camera scan auto-adds one unit
   to the cart at once (supermarket-till behaviour, with a ~2 s
   per-code cooldown so holding a barcode up does not add it
   repeatedly), while a TYPED barcode adds when Enter is pressed or
   Add to Order is clicked (D-25). The difference is deliberate:
   typing is a deliberate, reviewable action — the cashier sees the
   description, sets the quantity first, then confirms — while a
   camera scan IS the confirmation, like passing an item over a till
   scanner. Both paths enforce stock through `OrderService`. The
   **Description** button shows the same description for a typed
   barcode without adding it, as required for the cashier; scanning
   in the Camera screen also shows the product when one exists
   (manager/cashier view).
4. **Lowercase data filenames (D-19 — the project's decision):** the
   spec writes `Product.txt` / `Order.txt` / `Bill.txt`, but the code
   reads `Data/product.txt`, `Data/order.txt`, `Data/bill.txt` (and
   `Data/BillDetail.txt`, `Data/PCategory.txt`, `Data/Seasons.txt`).
   The lowercase names are kept as the project's decision; instead
   of renaming, `RecordFile::locate` falls back to a
   case-insensitive filename match when the exact path is missing, so
   a spec-capitalised file still loads on a case-sensitive system.
   A genuine Phase 2 product line (11 fields, no categoryId) also
   parses — the category is derived from the barcode's first two
   digits.
5. **Seasonal discount base and combination (D-09):** the seasonal
   percentage is taken on (withoutTax + tax − productDiscount), and
   seasonal and special discounts are ADDITIVE on that same base, not
   sequential (10% seasonal + 8% special = 18% off, not 17.2%).
   The spec does not pin the base down in the files; this is the
   formula the code uses and the README worked example uses.
6. **Top-10 by remaining stock (D-08):** the list is highest-stock
   first — a literal reading of the spec's "descending". If the TA
   reads it as purchase planning (lowest first), the report is
   inverted; the zero-stock product (CHOCO) therefore sorts LAST.
   This is kept deliberately and stated here, not silently flipped.
7. **Decoder rotation limit (D-27):** the diagonal scanlines cover
   about 16 degrees of tilt (`atan(70/249)`); a barcode tilted much
   further, e.g. 45 degrees, has no scanline and fails. There is no
   blur/noise filter beyond the thresholds tried per scanline. The
   decoder DOES try the inverted scanline (`255 - v`) as a fallback,
   so white-on-black barcodes decode; `test_inverted.bmp` is a
   genuinely inverted `test_normal.bmp` and decodes to 123456.
8. **What `product.txt` quantity means (D-33):** the quantity column
   is the REMAINING stock after all recorded sales. Opening stock is
   not recorded anywhere, so a historical sale can never be
   reconciled against it from the files alone — OIL sold 10 in one
   order while the file lists 8 remaining, and the files cannot say
   whether the sale or the stock figure is wrong, so neither was
   rewritten. CHOCO at 0 is a sold-out product, and the code already
   refuses to sell it (`canSell` requires quantity ≥ 1 within stock).
9. **Product barcode images are tracked (D-35):** `barcodes/*.bmp`
   is deliberately NOT in `.gitignore`. Generated/product barcodes
   live in `barcodes/` and must be tracked, so a fresh clone has the
   images the catalogue points at. `tests/` holds test images only.
10. **No full cross-file transaction (D-02 — remaining limitation):**
    checkout writes order, details, bill, then stock, with every
    return checked, stock decreases rolled back on failure, and
    `writeAllLines` writing `file.tmp` then renaming. A crash in the
    window between the bill write and the stock decreases still
    leaves a bill with unmoved stock; that window cannot be closed
    without real transactions, and the failure is now reported
    (`"stock-failed"`, `OrderService::dataHealth()`) instead of
    silent.

## Responsive layout

The UI fills the whole window (layout width grows past 1000 units,
right-hand panels anchor to the edge — no letterbox bars, nothing
floats on a pink background). The sidebar/content textures render at
the window's native pixel resolution, and all text is rasterised at
its on-screen size (`theme::uiScale`, set in `applyView()`) — the old
magnification blur is gone. Mouse coordinates go through the same
view, so clicks stay pixel-exact at every size. (An older version of
this document said the canvas was letterboxed; that was the old
layout and contradicted this paragraph — the fills-the-window claim
is the current one.)

## Tests

- `--selftest` — 44/44: a 6-code in-memory encode/decode round trip,
  the 7 stress BMPs in `tests/` decoded from disk
  (normal/rotated/inverted/blurry/noisy/small/large), and a
  decode-back check of all 31 BMPs in `barcodes/`. (It was 6/6 before
  v35, when no file was ever decoded by any self-test.)
- `tests/pos_selftest.cpp` — 127 headless checks of the Phase 2/3 modules:
  Data/product.txt parsing, the stock rule, merged-quantity rejection, totals
  (480 + 25 - 10 = 495 before seasonal discount; the same basket is
  445.50 inside the active 10% season — seasonal 49.50 — see the v32
  section), id formats, all four files' round-trips, and
  the stock decrease after a sale. Run it only from an empty folder
  (`mingw32-make -f Makefile.gui test`); it refuses to run against a
  non-empty catalogue, because it seeds (truncates) `Data/*.txt`.

## Mid-running sample data (v5)

The project ships as a store that is already operating:

- `Data/product.txt` — 24 products (count + weight units, varied tax and
  discount, one low-stock item at 3, one out-of-stock item at 0 so the
  stock rule can be demonstrated). Every product's barcode BMP exists
  in `barcodes/` and decodes back to its barcode (checked by
  `--selftest`); `tests/` holds test images only.
- `Data/order.txt` / `Data/orderDetail.txt` / `Data/bill.txt` — past orders
  (2026-11-14 to 2026-11-18) with mixed statuses (paid, on-hold,
  in-progress) and payment modes (cash, card/ABL, wallet/JazzCash,
  wallet/EasyPaisa). Every Order Id ends with the day of its date and
  every bill's totals reconcile with `BillCalculator`.
- The history is hand-seeded and maintained by hand. It did NOT come
  from `tests/seed_history.cpp`: that file did not compile against
  the current code (wrong constructor arity, fields removed from
  `Bill`, old per-product bill format), so the shipped data provably
  never came from it. It was deleted in v35; the "can never drift"
  claim in the older text below is withdrawn.

## Maintenance review (v5)

Checked while adding the data, and fixed where found:

- `OrderService::checkout` verifies **every** append (order, all
  details, all bill rows) and reports `write-failed` if any write did
  not reach disk, instead of trusting them silently. Since v35 it
  also checks every stock decrease and rolls back the decreases
  already made on failure, and `writeAllLines` writes `file.tmp`
  then renames — but there is still no full cross-file transaction
  (see decision 10 above).
- The payment-mode and order-status lists live in exactly one shared
  table (`src/domain/enums.h/.cpp`) used by both the GUI and the
  services (DRY — a SRP/quality fix). The merchant allow-lists are
  the exception: they live in the GUI drop-downs AND in
  `OrderService::validatePayment` (see "One rule, one place" above).
- The Products screen paginates (8 per page, Prev/Next) so the list
  stays usable as the catalogue grows — the screen was extended, the
  widgets and services were not modified (OCP).
- Known limitation, accepted for this scope and then fixed in v35:
  Order/Bill Ids are time-based (`HHMMSS` + day), so two bills
  generated within the same second used to share an Id. `checkout()`
  and `holdOrder()` now check `loadAll()` / `findById` at generation
  and regenerate (up to 5 tries) on a collision, so same-second
  checkouts get distinct Ids. The single home for the scheme is
  `IdGenerator`.

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
- The corner toast box is gone (feedback stays in the screens as
  message lines, and the POS screen keeps a 3-message history) and
  generated/decoded barcodes appear instantly -
  the reveal/sweep animation was removed. The mascot is gone too.

## POS live camera view

The camera worker always decoded frames on the POS screen (a scan
fills the barcode box), but the POS page never DREW the feed. Now,
while the camera runs, the POS top-right card is a **Live Camera**
card: the live feed with a LIVE pill and the FULL product description
alongside it (name, price, tax, discount, stock) — the description
used to be replaced by the camera card; since v35 both are shown.
When the camera stops, the card returns to the product description.
Camera failure shows the reason on the POS screen with a "use Read
from File instead" fallback.

## Unknown-barcode feedback

A successfully decoded barcode that has no product in
Data/product.txt used to show nothing on the Camera page. Now every
scan surface answers: Camera and File pages show "No product found
for barcode X in Data/product.txt" in the error colour, and the POS
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

## Phase 3 — PoS v2: categories, seasons, bill split, roles, reports

Phase 3 keeps the Phase 2 architecture (domain / persistence /
services / ui) and extends it; nothing from Phases 1-2 was rewritten.

- **Categories.** `Data/PCategory.txt` holds one category per line
  (Category Id, Name, Acronym, ending `#`). The Category Id is stored
  in `Product.txt` as the LAST field of each product line, and the
  first two digits of every product barcode ARE the category:
  `CategoryService::makeBarcode` builds a barcode as category id +
  the FULL product id (1001 in category 11 -> `111001`; category
  `00` is refused, and an id that cannot fit the 6-digit scheme,
  i.e. id > 9999, is refused — so 1001 and 11001 can no longer
  collide). Existing products were migrated to category-prefixed
  barcodes by hand, so a migrated barcode's last four digits need
  not equal the product id; only the two-digit prefix is contractual.
  Editing a product keeps its existing barcode unless its category
  changes (the barcode is printed on shelf labels and named by the
  barcodeFile column).
- **Bill split (requirements 8 + 9).** `Bill.txt` now holds ONE line
  per bill with bill-level money (14 fields + `#` — see the File
  formats section): total without tax, total tax, total product
  discount, total payable, amount received, balance returned,
  payment status (mirrors the order's status), seasonal discount,
  special discount, date, time. The per-product lines moved to
  `Data/BillDetail.txt` (Bill Id, acronym, unit price without tax,
  quantity, line discount = per-unit discount x quantity). Discounts
  are stored as rupee AMOUNTS in Bill.txt; the special discount is
  entered as a percentage at checkout and capped at 8%. The payable
  formula lives in `BillCalculator` (`BillService::buildBill` builds
  the bill row from its totals):
  withoutTax + tax - productDiscount - seasonal - special.
- **Seasons.** `Data/Seasons.txt` lines are: start date, end date,
  discount percentage, isActive (`true`/`false`), ending `#`. On
  checkout, `SeasonsService::activeDiscountPercentOn` finds the
  active season covering today's date and the percentage is applied
  to (withoutTax + tax - productDiscount); the resulting amount is
  stored in the bill's seasonal discount field. Seasonal and special
  are additive on that same base (see decision 5). Overlapping ACTIVE
  seasons are refused, a season of 100% is refused, and dates are
  validated for real (v34/v35).
- **Hold / resume.** Holding an order writes the order (status
  `on-hold`) and its detail rows only - no bill, no stock movement.
  Stock moves once, at checkout, when the resumed (or fresh) order is
  billed. Resuming reloads the order's lines into the POS cart by
  product barcode. Resuming re-checks stock and warns (`"low-stock"`)
  when stock has run out; checking out a resumed order completes
  THAT order in place (same Order Id) and refuses to bill an order
  that already has a bill (`"already-billed"`). Holding does NOT
  reserve stock — two held orders can both sell the last unit, and
  the second checkout fails at bill time. Resuming with a non-empty
  cart asks first (click Resume again to confirm the discard).
- **Editing a paid order.** The gate is `OrderService::editPaidOrder`:
  the order must be paid, within 7 days of its PAYMENT date (the
  bill's date, not the order/hold date — fixed in v35; the old text
  here said "order date" and was wrong), and a manager must have
  approved (the Manager Approve button only works in Manager role,
  behind a PIN prompt since v35, and approval is per-order: a fresh
  load resets it, a successful save spends it, and Save re-checks
  the CURRENT role). `saveEditedPaidOrder` then rewrites the order
  row, replaces its detail rows, and re-issues its bill IN PLACE -
  same Bill Id, since Bill Ids cannot repeat - carrying the original
  special discount, amount received and bill date through (the old
  text said the special discount reset to 0 and the bill date was
  rewritten to today — that was the v31 bug, fixed in v35) and
  recomputing the seasonal discount for the ORIGINAL bill date, and
  reconciling stock: old quantities are returned to the shelf first,
  then the new quantities are deducted, with rollback on failure.
- **Roles.** `UserRole` (Cashier / Manager) is a GUI-level switch,
  and switching to Manager now asks for the manager PIN (checked
  against a stored hash, never the digits). Cashier sees the selling
  screens; the manager screens (Categories, Seasons, Inventory,
  Product Edit, Reports, Order Edit approval) belong to the Manager
  role, `switchView()` enforces the role, and Order Edit Save
  re-checks it.
- **Reports.** `ReportService` builds the six manager tables from the
  repositories. Incomplete/completed reports are based on ORDERS
  (joined to bills for amounts), not on bills alone, so a held order
  is visible. "Cash sales" joins bills to their orders, uses the
  bill/payment date, and keeps only payment mode `cash`; the weekly
  window is today plus the previous 6 days, computed with real
  calendar maths. Top-10 sold aggregates by product over paid bills
  only, ties broken by acronym; top-10 by remaining stock is
  highest-first (see decision 6). A custom From>To returns an
  explicit error, not an empty table.
- **PDF bills.** `services/pdf_bill` writes a minimal, valid PDF by
  hand (no external library), saved as `bills/<Bill Id>.pdf` next to
  the executable. Any bill can be printed, not just the last one in
  the session (Bill Print screen: enter any Bill Id and Load), long
  bills paginate (new page when the cursor drops below 120pt, totals
  block moved to a fresh page when it would not fit, header repeated
  with "(continued)"), and on Windows the PDF is opened after writing.

## Fewer presses (v21)

A usability pass over the Phase 3 screens, GUI layer only - no
service, persistence or format changed:

- **Enter confirms.** One dispatch point (`onEnterKey`) maps Enter in
  a focused text box to the screen's main action - the same function
  the button calls, so keyboard and mouse can never disagree (DRY):
  barcode/qty -> Add to Order, amount received -> Generate Bill,
  generator code -> Generate, file path -> Decode, and the id/date
  boxes on the manager screens -> Load / Add / Run.
- **Bill close starts the next order.** The bill overlay's Close now
  calls `posNewOrder()` instead of re-implementing half of it:
  one click leaves a clean, focused POS ready for the next customer.
- **Reports run on selection.** Picking a report from the drop-down
  runs it immediately; the Run button remains for re-running with
  custom dates.
- **Seasons toggle in one click.** Clicking a season's Active column
  toggles it (previously: click row, then click Toggle Active).
- **Entering POS focuses the barcode box** (`switchView`), so
  scanning/typing works immediately on arrival.
- Two row hit-areas that had drifted from their drawings were
  re-aligned (Seasons rows are drawn at y=368 step 40, Held Orders
  at y=168): previously a click selected the neighbouring row.

## Full-project fix pass (v22)

A screen-by-screen audit of every view (draw code vs click code vs
rendered screenshots) found and fixed:

- **POS customer section was invisible.** `drawPosOrder` positioned
  the customer/contact boxes and the mode/status/merchant drop-downs
  but never drew them (only the drop-down *lists* were drawn). The
  cashier was asked for a customer name that could not be typed.
  The section now draws as a proper card with labels.
- **Special % and Amount Received could not be typed into** - their
  `handleClick` was never called, so they could never take focus.
- **Totals card overflowed** the 680-unit layout height (584 + 116 =
  700) and was clipped at the window bottom; the right column was
  re-stacked to fit.
- **Inventory row clicks were offset** (tested at y=200, drawn at
  y=284) - the third screen with this defect after Seasons and Held
  Orders; all row hit-areas are now aligned with their drawings.
- Hold/Resume buttons straddled the cart card border (card shortened);
  Categories list ran past the screen (capped at 8 rows, compressed);
  the Product Edit overflow note overlapped the last row; the Order
  Edit approval pill overlapped the Approve button and its field
  label struck through the heading; the Bill screen's Close button
  was painted over by its own card in the empty state.
- Order Edit's save armed the POS bill overlay (`m_showBill`), which
  would have popped up stale on the POS screen; removed.
- Money and percent boxes now accept a decimal point (an opt-in
  `setAllowDecimal` on the text box widget, one '.' at most).
- The never-drawn, never-clicked `m_roleBtn` widget was removed
  (role switching uses the sidebar pill's own hit-rects).

## Enter key consolidation (v23)

A warnings pass (`-Wall -Wextra`) exposed a real bug: the app had
TWO Enter handlers. The original `handleEnterKey()` fired on the
KeyPressed event (Generator, File Scanner, Camera, POS, Self-Test),
and the v21 `onEnterKey()` fired on TextEntered - so on the POS
screen one Enter press added the scanned item TWICE, and Generate /
Decode ran twice. There is now exactly one Enter path
(`onEnterKey`), which keeps the legacy behaviours (invalid code
shakes with a hint, camera Enter unlocks a locked scan, Self-Test
Enter re-runs) and the v21 focus-gated POS/manager actions. The old
handler also added POS items even while typing the customer name;
Enter is now focus-aware there. Also removed: the unused `pkrShort`
helper, and a member-initialisation order warning in Product Edit.

## One path per job (v25)

Point 10 taken further, at the user's direction:

- **The sidebar mascot is gone.** Its speech bubble and character
  were drawn over the lower nav entries; in Manager mode (13
  entries) Edit Products, Reports and Self-Test were partly covered.
  The sidebar now shows only navigation.
- **One camera.** Camera barcode reading existed twice: the Phase 1
  Camera Scan screen and the POS Start Cam. The sidebar entry for
  the standalone screen was removed - the POS camera (which adds
  the scanned item automatically) is the single camera path. The
  decoder/camera modules are unchanged and shared.
- **Zero-click description.** Typing a complete 6-digit barcode in
  the POS shows the product description by itself; the Description
  button was removed and Add to Order widened in its place. Unknown
  codes still produce the clear not-found message. (v35: the
  Description button is back — a real button that describes the
  typed/scanned code WITHOUT adding it. Typing still auto-describes;
  the button exists because the spec asks for one.)
- **Picking a held order in the POS drop-down resumes it
  immediately** (the Resume button remains for re-picking the order
  already shown).

## Modal bill, phantom pill and text clearance (v26)

- **The bill overlay is truly modal now.** While it is open, mouse
  clicks are answered by its two buttons only - the sidebar toggle,
  the navigation and the screen behind it ignore clicks, typing is
  swallowed, and the number-key screen shortcuts are disabled. The
  toggle and the particle effects are not even drawn while the bill
  is up: they stay in the back frame behind the dim layer, so the
  layout can no longer shift under an open bill.
- **Bill overlay reflowed**: the card is taller (552) and the totals
  block uses 22-unit rows, so the Status line ends well above the
  Close / Print buttons - previously the buttons were painted over
  the Balance and Status rows.
- **The logo heart now follows the "Chili Mili" title** instead of
  preceding it (position measured from the rendered title width).
- **The duplicate role pill is removed.** A second "Cashier /
  Manager Mode" pill was drawn at the top-left of every screen, but
  it was rendered into the content texture after that texture had
  already been presented, so it never actually appeared in the
  running app - it only polluted QA screenshots and left an
  invisible clickable strip over the screen headings. The sidebar's
  Role pill is the single role switch.
- **Label and card clearance pass** (text was grazing the element
  below it in several places): Order Edit's form rows moved down so
  the "Order Id" label is no longer struck through by its input box,
  Product Edit's first label row no longer touches the "Product"
  heading, Inventory's eight rows now end inside their card instead
  of the last row overflowing it, and the Reports controls card is
  taller so its drop-down no longer merges with the card edge.

## Message placement audit (v27)

A saved-PDF note in the bill overlay landed on top of the Status
line. The whole project was swept for the same defect class - a
message or text line sharing its band with another element:

- **Bill overlay**: the print note now sits in the empty space to
  the right of the Print as PDF button (its width is measured; on a
  window too narrow for that space it drops to its own clear line
  under the buttons). The buttons also moved up 12 units, so the
  Status line, the note and the buttons each own their band.
- **Inventory**: the stock message was drawn at the search box's
  own position and would have painted over it; it now has its own
  strip under the filter card, and the product list shifted down.
- **Bill Print screen**: with four or more bill lines, the last
  line's band reached the Subtotal line; the totals block moved
  down 14 units.
- **Held Orders**: the message sat flush on its card's bottom
  border; the card is 8 units taller.
- **Reports**: the message line moved 4 units clear of the summary
  line above it.

## Segmented role switch and POS spacing (v28)

Point 12 asks for the cashier and manager features to be provided
separately through the UI. The separation existed, but the control
for it was a small pill that was easy to miss. Two changes:

- **Segmented role switch**: the sidebar now carries a two-segment
  control under the title - CASHIER and MANAGER always visible side
  by side, the active segment filled with the primary colour and
  white text. Clicking a segment selects that role (clicking the
  active one does nothing); switching to Cashier while a manager
  screen is open still returns to the POS screen. The menu entries
  moved down (they start at y=146 in both the drawing and the click
  handling) so the title, the switch and the menu no longer crowd
  each other.
- **POS bottom reflow**: the bottom band of the POS screen was
  congested, and one overlap was real - the held-orders dropdown
  (x=534, width 180) slid 24 units under the totals card. The entry
  and description cards are shorter (156), the cart and customer
  cards moved up, the totals card is taller (96) with wider row
  spacing and its bottom edge clear of the window edge, the
  line-count text sits beside the Order Items heading instead of
  under the dropdown, and the held-orders dropdown is narrower
  (148) and ends before the right column starts.

## Role slider and mirrored camera preview (v29)

- **Role slider**: the segmented Cashier/Manager control is now a
  slider - a single thumb that glides between the two ends of the
  track when the role changes (eased in update(), the same way the
  sidebar itself slides). Clicking the Cashier half or the Manager
  half of the track selects that role, as before.
- **Mirrored camera preview**: the live camera view is flipped
  horizontally, so it behaves like a mirror - moving right moves
  the picture right. The flip is display-only (the preview sprite
  is drawn with a negative x scale); the frames the barcode
  decoder works on are untouched, so scanning is unchanged.

## Selection highlight, roomier Products, bigger catalog, true resume (v30)

- **Selected rows are unmistakable**: the theme's Active colour was
  byte-for-byte identical to LightPanel, so a "selected" row looked
  exactly like an unselected one. Active is now a clearly deeper
  pink, and every selectable list (Held Orders, Seasons, Inventory,
  Edit Products) also draws a primary-pink bar at the left edge of
  the selected row - the same accent the sidebar uses.
- **Products breathes**: 6 taller rows per page instead of 8 packed
  ones (row height 48 -> 58, wider gaps, larger sub-line); paging,
  the mouse wheel and the scrollbar follow the new page size.
- **Inventory covers the whole catalog**: the list now scrolls
  (mouse wheel + slim scrollbar, like Products) instead of stopping
  at the first 8 products. The catalog grows from 16 to 24 products
  (one new product per category, barcodes generated with the
  project's encoder and decode-verified).
- **Resume is complete**: resuming a held order restores its
  customer name, contact and payment mode/merchant into the POS
  form, and checking out completes THAT order in place - same
  Order Id, its row and detail rows replaced, the bill pointing at
  it - instead of leaving a ghost on-hold order that could be
  resumed and paid twice.

## Phone format, honest Order Edit approval, product BMPs (v31)

- **Phone numbers**: contact boxes now format as you type - the
  dash appears by itself after the fourth digit (0300-1234567,
  11 digits / 12 characters) - and the dashed form is accepted
  everywhere a number is checked (the held orders in the data
  already carry dashed numbers; before, resuming one truncated
  the number to 11 characters and then rejected it). Hold and
  Order Edit apply the same check as checkout.
- **Order Edit approval means something**: loading an order no
  longer needs approval (viewing is not editing), approving
  requires a loaded order, approval is per-order (a fresh load
  resets it, a successful save spends it), and Save always acts
  on the loaded order - not on whatever the id box happens to
  say. Unmapped save errors can no longer fall through to a
  false "updated" message.
- **Product barcode images are written**: saving a product in
  Add / Edit Products now writes its barcode BMP if the file is
  missing, so a UI-created product can really be scanned from
  its file.
- **Hardening**: a strict shared date validator (real month/day
  ranges, including leap years) now guards the report week maths,
  the 7-day edit gate and the custom report range. (This was claimed
  at v31 but was false then — `isValidDate` accepted 2026-02-31 —
  and was only fixed for real in v34.) `src/barcode/encoder.h` no
  longer repeats `common.h`'s default arguments, so it can be
  included like any other header.

## Fresh paid order for the Order Edit demo, and a record-append fix (v32)

- **The problem found on 2026-11-26**: the Order Edit screen was
  always empty, not because the edit code was broken, but because
  every paid order in the shipped data was more than 7 days old (the
  newest was 2026-11-18, 8 days before the demo), so the 7-day gate
  refused every load with "More than 7 days have passed."
- **Fix**: one fresh paid order, created by the real checkout code:
  order 00240326, 2026-11-26, Ayesha Khan, 0300-5551234, cash, paid.
  Lines: Fresh Milk x2, Branded Bread x1, White Sugar x1. Bill
  100240326: without tax 690.00, tax 33.00, product discount 15.00,
  seasonal discount 70.80 (active 10% season), payable 637.20,
  received 1000.00, balance 362.80. Stock in product.txt decreased
  for the three lines (milk 26->24, bread 25->24, sugar 40->39).
  Load it in Order Edit to demo the approve-then-save flow; it stays
  editable until 2026-12-03.
- **A second, worse bug found while seeding it**: Data/bill.txt and
  Data/BillDetail.txt ended without a trailing newline, and
  RecordFile::appendLine wrote the new record straight onto the
  last line. The reader is line-based and the bill parser requires
  the 15th token to be exactly "#", so the glued line failed to
  parse and BOTH bills on it vanished (12 bills read as 10, 20
  bill details as 18). The two files are repaired (newline added
  at the junction), and appendLine now ends a file that does not
  already end with a newline before appending, so this can never
  happen again — verified by stripping the newlines and running a
  real POS checkout, which landed cleanly on its own line.

## Reports drop-down list was hidden behind the results card (v33)

- **The bug (found by Talha from a screenshot, 2026-11-26)**: on the
  Reports screen, opening the report-type drop-down showed only the
  first option ("incomplete"). The drop-down holds 10 options
  (incomplete, completed, daily, weekly, monthly, yearly, custom,
  sales_discounts, top_sold, top_stock) and its list is 10 rows tall
  (y 162..482), but drawReports drew the list BEFORE the results
  card, and the card (top y 192) painted over rows 2..10. Clicking
  still worked on the hidden rows — only the drawing was wrong.
- **Fix**: m_reportDrop.drawList() now runs on top of the results
  card — the same rule the POS screen already follows ("open
  drop-down lists draw last"). It is drawn in both branches: the
  empty-table branch returns early, so a single call at the end
  of the function never drew while no report had been picked yet
  (that was the first attempt, caught by the screenshot check),
  and drawing it only in the empty branch would leave it under
  the table rows once a report is loaded. No other screen had
  this problem: Product Edit draws its lists last, and the POS
  mode/status/merchant lists already draw last.


## Shipped-data cleanup and honest date validation (v34)

- **Dirty seeded rows cleaned (2026-11-26, audit D-33)**: the
  hand-seeded `Data/order.txt` still carried test text a TA can read
  aloud — customers `qwerty`, `talha`, `bilal`, `qwertyu`, merchants
  `JC` and `koi_bhi`, and `cash JazzCash`. They are now realistic
  names (Usman_Ali, Hassan_Ahmed, Ali_Raza, Fatima_Khan), real
  merchants (JazzCash, or `-` for cash), and dashed 11-digit contacts
  (`0987-6543211`, `0321-7654321`, `1234-5678909`, `1234-5678876`,
  `1234-5678998`); the same customer names were fixed in
  `Data/bill.txt` to match. Nothing else about those orders changed.
- **Bills for unpaid orders removed**: five shipped bills claimed full
  payment received for orders that were on-hold or in-progress
  (118024716, 111480918, 110262319, 110273519, 111405219), which
  contradicts the code — holding an order writes order + details only,
  never a bill — and made resuming one of those orders bill it a
  second time. Those five bills and their seven BillDetail rows are
  deleted; `Data/bill.txt` now holds only the 7 paid bills (total
  payable 12968.20, all statuses `paid`, every bill's order exists,
  every BillDetail row's bill exists). The five orders themselves stay
  on-hold / in-progress in `Data/order.txt`, so Held Orders and the
  resume demo still work — resuming one now creates its FIRST bill.
- **Unexplained barcode images deleted**: `barcodes/barcode_765.bmp`
  (no code path can produce a 3-digit barcode) and the two extra files
  found on GitHub, `barcode_234567.bmp` and `barcode_665465.bmp`
  (valid BMPs, no matching product). `barcodes/` now holds only
  generated samples.
- **Date validator fixed, not just documented**: `isValidDate`
  (src/common/date_time.cpp) used to accept any day 1–31 in any month,
  so a season starting 2026-02-31 was accepted. It now checks real
  month lengths with leap years (2024-02-29 passes, 2026-02-29 and
  2026-02-31 do not). All shipped dates are real, so no seeded row
  is affected.
- **Totals example corrected**: the Tests section said
  `pos_selftest` had 21 checks (it has 69) and quoted the basket as
  495 with no mention of the season. It now states the pre-seasonal
  495 and the in-season 445.50 side by side.

## Full-project fix pass (v35) — audit deficits D-01 to D-38

A three-batch fix pass against the ISE audit of v31
(`ISE_Audit_Report_Barcode_Scanner_v31.md`, 38 deficits). Batches A
(services + data layer) and B (GUI) ran first; batch C (barcode /
camera / tests / build / docs) closed the rest. Verification for the
whole pass: full build with `-Wall -Wextra`, zero warnings;
`--selftest` 44/44; `tests/pos_selftest.cpp` 127/127 from a fresh
empty folder. `Data/` is unchanged from v34 except `product.txt`'s
barcodeFile column (D-37) — verified by diff.

### Batch A — services + data layer

- **D-01 Ids:** `checkout()`/`holdOrder()` check `loadAll()` /
  `findById` at generation; on collision they sleep 1 s and
  regenerate, max 5 tries, then return `"id-collision"`. Same-second
  checkouts now get distinct Ids (tested).
- **D-02 atomicity:** `writeAllLines` writes `file.tmp` then renames;
  every `decreaseStock` return in checkout and paid-order edit is
  checked, with rollback of decreases already made;
  `OrderService::dataHealth()` reports paid orders with no bill,
  bills with no order, orphan BillDetail rows. Residual limit: a
  crash between the bill write and the stock decreases still leaves
  a bill with unmoved stock (see decision 10).
- **D-03 dropped lines:** every repo counts unparsable lines on load
  (`droppedLines()`), and `saveAll` writes a `.bak` copy first when
  lines were dropped. The Self-Test screen shows per-file counts.
- **D-04 dates:** one real `daysBetween` (`date_time.cpp`, days since
  epoch); the 7-day gate runs on the PAYMENT date (the bill's date);
  checkout of a resumed order sets the order date/time to today.
- **D-05 PIN:** `pin_hash::fnv1a()` + `matchesManagerPin()`; only the
  hash is stored, never the digits (the v35 comment fix removed the
  digits from a comment that still spelled them out).
- **D-06 payments:** `validatePayment` in checkout and hold — cash →
  merchant `-`; card → ABL/MCB/Meezan; wallet → JazzCash/EasyPaisa/
  Raast; anything else is `"bad-payment"`. Underpayment is refused
  (`"underpayment"`); amount received ≤ 0 is never rewritten to full
  payment; checkout with a non-paid status is `"bad-status"`.
- **D-08 reports:** incomplete/completed are order-based; cash sales
  use the bill/payment date and only paid bills; `allSalesWithDiscounts`
  excludes non-paid bills and sums a total discount; `topTenSoldItems`
  aggregates by product over paid bills; custom From>To is an error,
  not an empty table. `topTenRemainingStock` stays highest-first
  (decision 6).
- **D-11 (service part):** `OrderService::archiveOrder()`; held-delete
  in the GUI routes through it.
- **D-15:** `<=` stock rule KEPT as a deliberate deviation (decision
  2); one `modifyStock`; negative quantities rejected in all three
  stock functions; `changeQuantity` returns an error string;
  quantities summed per product before the stock check.
- **D-16 paid-order edit:** original special discount, amount
  received and bill date carried through; seasonal recomputed for the
  ORIGINAL bill date; stock rollback on write failure.
- **D-17 seasons:** overlapping ACTIVE seasons refused; discount
  capped below 100%; `setActive` keyed by (start, end) date pair.
- **D-19 formats:** Phase 2 product lines (11 fields, no categoryId)
  parse, category derived from the barcode; `RecordFile::locate`
  falls back to a case-insensitive filename match.
- **D-20 categories:** `makeBarcode` uses the FULL product id (no
  more `% 10000` collisions), refuses `"00"` and ids > 9999;
  `updateProduct` has the same duplicate-barcode check as `addProduct`.
- **D-21 validation:** acronym 1–8, unit in {count, weight}, price >
  0, tax ≥ 0, discount ≥ 0 and ≤ price, qty ≥ 0 — in the service, for
  add and update; `addProduct` sets `barcodeFile` like `updateProduct`.
- **D-23:** resumed checkout supersedes the on-hold row (same Id);
  checkout refuses an order that already has a bill
  (`"already-billed"`); `resumeOrder()` re-checks stock and returns
  `"low-stock-warning"` with the lines still loaded.
- **D-30 paths:** `locate()` tries the exe directory first (Windows),
  then the existing candidates.
- **D-31 dedup:** one `daysBetween`, one payable function
  (`BillCalculator`), one stock-adjust function (`modifyStock`).
- **D-32 money:** `round2` applied at every payable step;
  `buildBill` returns `std::nullopt` when the written fields do not
  sum to the written payable.

### Batch B — GUI

- **D-05 PIN:** a PIN prompt overlay on the role slider, on Order
  Edit approval (always, even if already Manager), and on entry to
  manager-only screens; `orderEditSave()` re-checks the CURRENT role.
- **D-07 PDF/screens:** Bill Print is in the sidebar (any Bill Id +
  Load + Print); Camera Scanner is wired back in. The PDF paginates
  (new page below 120pt, totals block on a fresh page, header
  repeated with "(continued)"); Windows opens the PDF after writing.
- **D-12 (GUI part):** `stopCamera()` ALWAYS joins the worker when
  joinable — the `std::terminate()` fix. The worker stops after
  >120 consecutive failed captures (~3 s) and the failure is shown
  on the POS screen with a file-scan fallback.
- **D-18:** special discount outside 0–8% is rejected in the GUI,
  no bill; the clamp is deleted.
- **D-20 (GUI part):** the Generator screen has a category dropdown
  and requires Manager role + a selected category + a code starting
  with that category's id.
- **D-23 (GUI part):** resume with a non-empty cart arms first
  ("Click Resume again…"), second click goes through;
  `"low-stock-warning"` is success-with-warning; `"already-billed"`
  is surfaced.
- **D-24:** POS Describe/Add/Add-One call `isValidCode()`; empty and
  invalid codes get their own messages.
- **D-25:** scrollable POS cart (all lines editable) and bill overlay
  (past 5 rows); `changeQuantity()` errors surfaced; ~2 s per-code
  scan cooldown; an unknown code is reported once until it changes;
  the app starts on the POS Order screen. Order Edit lines scroll
  past 4 too.
- **D-26:** no toast anywhere — `showToast()`, all call sites,
  `m_toast` and the `ToastNotification` class are deleted. POS keeps
  a visible 3-message history instead.
- **D-28:** a real Describe button on the POS screen; the camera card
  shows the full description alongside the feed; camera failure
  shows its reason on the POS screen.
- **D-29:** Product Edit lists ALL products with search + pagination;
  the loaded product's Id is the update key; generated BMPs go to
  `barcodes/` and the `saveBMP` return is checked.
- **D-38 (GUI part):** font failure shows a Windows message box
  before throwing; the Self-Test screen lists the CLI modes.
- **D-11 (GUI part):** `handleEvents()` split into 14 per-screen
  handlers + dispatch; `heldDeleteSelected()` routes through
  `OrderService::archiveOrder`; Self-Test screen shows data health
  and dropped-line counts; card merchant dropdown narrowed to
  ABL/MCB/Meezan; `seasonToggle()` uses the (start, end) overload.
  What is STILL God-class-like is listed under "SRP, honestly" below.
- **D-02/D-03/D-30 (GUI part):** data-health and dropped-line counts
  on the Self-Test screen; the empty-Products message shows the
  resolved absolute path tried.

### Batch C — barcode / camera / tests / build / docs

- **D-10 555555:** removed as the demo barcode everywhere — the
  in-memory samples in `src/main.cpp`, `src/App.cpp` and the GUI
  self-test screen now use 145555 (White Sugar); `tests/barcode_555555.bmp`
  and `barcodes/barcode_555555.bmp` deleted. Canonical demo barcodes
  are 145555 and 111001. (555555 survives only as the code inside
  `tests/test_blurry.bmp`, a decode test with no product behind it.)
- **D-12 (console part):** `App::scanWithCamera` retries a failed
  capture instead of returning, up to the 30 tries, with an overall
  45-second deadline checked between attempts; the last good frame
  is still saved as `last_capture.bmp`. Camera init tries the next
  enumerated device once when device 0 fails (Media Foundation loop
  over the first two devices; Video-for-Windows `capDriverConnect`
  0 then 1).
- **D-13 tests:** `--selftest` and the console self-test now decode
  the actual `tests/*.bmp` stress files from the repo-root `tests/`
  dir (with fallback to the exe's folder, graceful when missing);
  `pos_selftest.cpp` REFUSES to run when `Data/product.txt` in the
  current directory holds more than the 2 seeded products (exit 2,
  clear message); `tests/seed_history.cpp` DELETED (did not compile;
  the shipped data did not come from it); `tests/test.txt` rewritten
  (no `setup_qa.bat` — it never existed; encoder path corrected to
  `src/barcode/encoder.cpp`; inverted file documented as genuinely
  inverted); `Makefile.gui` gains a `test` target that builds
  `pos_selftest` and runs it in a fresh temp folder.
- **D-14 build:** `run_gui.bat` searches PATH first, then
  `C:\msys64`, `D:\msys64`, `%USERPROFILE%\msys64` for `g++`; checks
  SFML headers AND libs up front with a clear message; refuses to
  rebuild while `barcode_studio_gui.exe` is running (tasklist check);
  checks the exe exists after the build; waits 3 s after launch and
  reports if the app is not running, instead of exiting 0. The bat
  was static-reviewed line by line against the known cmd traps (no
  unescaped parentheses in echo/rem text inside blocks, `if
  errorlevel` form, `%%VAR` in for-loops, quoted for-list items so a
  profile path with a space does not split); it was NOT run — there
  is no Windows on this VM — and the review caught two real bugs
  before delivery (the PATH-case SFML root, and the unquoted
  `%USERPROFILE%` list item). `Makefile.gui` creates `build_obj/`
  and its subfolders itself (order-only prerequisite), so a direct
  `mingw32-make -f Makefile.gui` works on a fresh clone.
- **D-27 decoder:** `decodeScanline` tries the inverted scanline
  (`255 - v`) as a fallback after the normal polarity; `test_inverted.bmp`
  replaced with a GENUINELY inverted `test_normal.bmp` (every pixel
  flipped; black fraction 0.405 → 0.595) and verified to decode to
  123456 — a control build with the fallback removed decodes it to
  nothing, proving the fallback is what makes it pass. `loadBMP`
  validates headers (file ≥ 54 bytes, header size ≤ 1024, pixel
  offset inside the file, width/height ≤ 10000, pixel data fully
  present) and rejects absurd dimensions with an error, not an
  exception; `readUInt16/32` return 0 on a short read instead of
  uninitialised bytes. Rotation limit documented honestly as ~16
  degrees (decision 7).
- **D-32 (screenshot part):** `--screenshot` is non-mutating:
  `main.cpp` resolves the output path, copies `Data/`, `barcodes/`
  and `assets/` to a fresh temp folder and `chdir`s there BEFORE
  constructing the GUI, so seeded QA checkouts never touch the real
  data (verified: all `Data/` md5s unchanged after screenshot runs).
- **D-34 camera formats:** `initialize()` fails loudly, naming the
  FourCC, when the camera negotiates an unsupported format; the
  raw-copy-as-pixels `else` branch in `readFrameRaw` is deleted
  (frame discarded, error logged); ALL members are initialised in
  BOTH camera constructors; the frame allocation is guarded with
  try/catch (`std::bad_alloc` reports and returns null) plus a
  sanity cap on frame size; the stride fallback no longer guesses
  `width * 4` for unknown formats.
- **D-35 build files:** `Makefile.gui` has the `mkdir` rule (D-14);
  `CMakeLists.txt` aligns with it (`-O2` for non-MSVC, `-D_WIN32`
  and `uuid` on Windows) and its post-build copy now covers `Data/`
  and `barcodes/` as well as `assets/` (not `bills/` — PDFs are
  generated at runtime); `.gitignore` adds `build_obj/`, `bills/`
  and `bills_out/`, and deliberately does NOT ignore
  `barcodes/*.bmp` (decision 9).
- **D-37 barcode dirs:** ONE directory — `barcodes/` for
  generated/product barcodes, `tests/` for test images only. The 24
  product BMPs were copied from `tests/` to `barcodes/`, and the
  barcodeFile column in `Data/product.txt` was repointed to
  `barcodes/barcode_<code>.bmp` (same filenames); `addProduct` and
  `updateProduct` now store `barcodes/…` so the stored path and the
  actual file agree (Product Edit already wrote there). Nothing
  else in `Data/` changed (diff vs v34 shows only `product.txt`).
- **D-09 (docs):** the README worked example now shows the in-season
  445.50 answer for the milk+bread basket (495.00 pre-seasonal,
  seasonal 49.50) instead of quoting 495.00 with no mention of the
  season; the v34 DESIGN.md fix of the same example was verified
  still correct.
- **D-36 (docs):** every false claim the audit listed was corrected
  in this document — record_file (production-only), UI logic, stock
  rule, money maths, DRY payment lists, console reusing services,
  toast/mascot, bill format (14-field bill-level + BillDetail),
  date validator (true after v34), hold/resume claims, checkout
  verification, category tails, BMP decode-back, layout
  contradiction, test counts (44/44 and 127, not 6/6 and 21/69),
  and the 495 example. Where the old text was wrong, the correction
  says what the old text claimed and that it was wrong.
- **PIN leftover (Batch A):** `src/common/pin_hash.h` no longer
  writes the PIN digits anywhere — the comment now states the hash
  value without them — and the PIN-acceptance checks in
  `pos_selftest.cpp` were removed for the same reason (only wrong-PIN
  rejection is asserted there; acceptance is exercised through the
  GUI prompt). Grep for the digits in `src/` + `tests/` returns
  nothing except barcode filenames that happen to contain them.

### SRP, honestly (D-11 — what is still not split)

Batch B split `handleEvents()` by screen and Batch A collapsed the
payable/date/stock duplications, but these classes still have more
than one reason to change, and the viva answer should name them:

- `BarcodeStudioGui` (~4,000 lines, ~220 `m_*` members) still holds
  all 14 screens, the camera worker thread, and the `screenshot()`
  QA hook (now non-mutating, but still test code in the production
  class). The split would be one class per screen plus a navigator,
  with the camera session and the QA hook moved out.
- `OrderService` still owns the cart, checkout, hold/resume,
  paid-order edit + 7-day gate, and stock reconciliation. The split
  would be a cart service, a checkout service, and a paid-order edit
  service, sharing one `BillCalculator`.
- There are still no `I*Service` interfaces — services depend on
  repo interfaces, but the GUI holds concrete services — and a new
  discount type or payment mode still touches several files (the
  fixed `seasonalDiscount`/`specialDiscount` fields, the 14-field
  serialiser, reports, PDF, GUI). A full split was out of scope for
  this pass; the per-screen handlers are methods, not separate
  screen classes.

### Verification (v35, all three batches)

- Full build (`g++ -std=c++17 -O1 -Wall -Wextra`, all `src/**/*.cpp`):
  zero warnings, exit 0. The Media Foundation camera branch is
  `#ifdef`'d out on Linux, so its edits were reviewed, not compiled
  here — same caveat as the `.bat`.
- `--selftest`: **44/44** (6 in-memory + 7 stress files + 31
  `barcodes/` decode-back). `--console` option 4: 13/13 (6 + 7 files).
- `pos_selftest` (built per the `Makefile.gui` test recipe, run once
  from a fresh empty folder, never the repo root): **127/127** (126
  from batches A+B, +1 net from the rewritten PIN checks in batch C).
  From the repo root it refuses with exit 2 (CWD guard verified).
- Data integrity recheck after the D-37 repointing: every OrderDetail
  order id + product id/barcode exists; every BillDetail bill id
  exists; every bill's order exists and is paid; every bill's
  subtotal/tax/discount/payable recomputes from BillDetail + product
  prices (0 mismatches); all `Data/*.txt` end with a newline
  (including `Data/PCategory.txt`, which never had one — the missing
  newline was added in the final v35 verification pass).
- POS totals card agrees with the bill (D-09, final pass): it used
  to show the pre-seasonal payable (495.00) while the bill charged
  445.50. It now shows Seasonal (10.00%): -PKR 49.50 (and Special
  when typed) as their own rows, and PAYABLE is the final amount
  after both — the same formula checkout uses. Rows appear only
  when the discount is non-zero.
- Screenshots (all in `~/workspace/your_files/`, `v35_` prefix, each
  visually inspected): `v35_pos.png` (POS, seeded Milk x2 + Bread
  x1), `v35_billprint.png` (Bill Print empty state), `v35_reports_dropdown.png`
  (all 10 report options open over the incomplete report, 5 rows,
  total PKR 3458.00), `v35_orderedit_00240326.png` (order 00240326
  loaded: Ayesha Khan, 0300-5551234, 3 lines with steppers; needed
  `TZ=Asia/Karachi` — the VM clock is UTC Nov 25 while the data is
  dated in PKT Nov 26). The Reports and Order Edit shots used a
  THROWAWAY copy with one-line hooks; it was deleted afterwards and
  the shipped tree contains no hooks (verified by grep). The shipped
  `--screenshot` runs in a temp copy of the data, so no screenshot
  run mutated the real `Data/` (md5-verified).

## v36 — two build-breakers found on first run on Windows (2026-11-26)

Both were introduced by the v35 build hardening and both were caught by
Talha running `.\run_gui.bat` on his PC. Both are fixed here; the v35
application code is untouched.

- `run_gui.bat`: one comment line had lost its `rem` prefix and read
  `creates them itself, so a direct make run works as well.` — a bare
  line, so cmd tried to run `creates` as a command
  ("'creates' is not recognized as an internal or external command").
  The `rem` is restored. Lesson for future edits: every comment line
  in this file must start with `rem`; a dropped prefix turns a comment
  into a command.
- `Makefile.gui`: the `$(OBJDIR)` (build_obj) folder rule was the FIRST
  target in the file, so it was make's default goal. With build_obj
  already existing (run_gui.bat creates it), plain
  `mingw32-make -f Makefile.gui` reported "'build_obj' is up to date"
  and compiled nothing, so no exe was produced — the bat's own
  "exe is missing" check then correctly stopped the run. Fix: `all`
  is now the first target (with a comment saying it must stay first);
  the build_obj rule remains as an order-only prerequisite of the
  object rules. Verified on Linux with `make -n`: the old file prints
  only "'build_obj' is up to date", the fixed file prints all 37
  compile commands plus the link step.
