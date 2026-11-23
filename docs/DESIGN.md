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

## Phase 3 — PoS v2: categories, seasons, bill split, roles, reports

Phase 3 keeps the Phase 2 architecture (domain / persistence /
services / ui) and extends it; nothing from Phases 1-2 was rewritten.

- **Categories.** `Data/PCategory.txt` holds one category per line
  (Category Id, Name, Acronym, ending `#`). The Category Id is stored
  in `Product.txt` as the LAST field of each product line, and the
  first two digits of every product barcode ARE the category:
  `CategoryService::makeBarcode` builds a barcode as category id +
  zero-padded product id (1001 in category 11 -> `111001`). Existing
  products were migrated to category-prefixed barcodes by hand, so a
  migrated barcode's last four digits need not equal the product id;
  only the two-digit prefix is contractual. Editing a product keeps
  its existing barcode unless its category changes (the barcode is
  printed on shelf labels and named by the barcodeFile column).
- **Bill split (requirements 8 + 9).** `Bill.txt` now holds ONE line
  per bill with bill-level money: total without tax, total tax, total
  product discount, total payable, amount received, balance returned,
  payment status (mirrors the order's status), seasonal discount,
  special discount, date, time. The per-product lines moved to
  `Data/BillDetail.txt` (Bill Id, acronym, unit price without tax,
  quantity, line discount = per-unit discount x quantity). Discounts
  are stored as rupee AMOUNTS in Bill.txt; the special discount is
  entered as a percentage at checkout and capped at 8%. The payable
  formula lives in `BillService::buildBill`:
  withoutTax + tax - productDiscount - seasonal - special.
- **Seasons.** `Data/Seasons.txt` lines are: start date, end date,
  discount percentage, isActive (`true`/`false`), ending `#`. On
  checkout, `SeasonsService::activeDiscountPercentOn` finds the
  active season covering today's date and the percentage is applied
  to (withoutTax + tax - productDiscount); the resulting amount is
  stored in the bill's seasonal discount field.
- **Hold / resume.** Holding an order writes the order (status
  `on-hold`) and its detail rows only - no bill, no stock movement.
  Stock moves once, at checkout, when the resumed (or fresh) order is
  billed. Resuming reloads the order's lines into the POS cart by
  product barcode.
- **Editing a paid order.** The gate is `OrderService::editPaidOrder`:
  the order must be paid, within 7 days of its order date, and a
  manager must have approved (the Manager Approve button only works
  in Manager role). `saveEditedPaidOrder` then rewrites the order
  row, replaces its detail rows, and re-issues its bill IN PLACE -
  same Bill Id, since Bill Ids cannot repeat - recomputing totals
  (special discount resets to 0; the current season, if any, is
  re-applied) and reconciling stock: old quantities are returned to
  the shelf first, then the new quantities are deducted.
- **Roles.** `UserRole` (Cashier / Manager) is a GUI-level switch.
  Cashier sees the selling screens; the manager screens (Categories,
  Seasons, Inventory, Product Edit, Reports, Order Edit approval)
  belong to the Manager role.
- **Reports.** `ReportService` builds the six manager tables from the
  repositories. "Cash sales" joins bills to their orders and keeps
  only payment mode `cash`; the weekly window is today plus the
  previous 6 days. Top-10 sold joins BillDetail quantities to the
  products' remaining stock.
- **PDF bills.** `services/pdf_bill` writes a minimal, valid PDF by
  hand (no external library), one page per bill, saved as
  `bills/<Bill Id>.pdf` next to the executable.

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
  codes still produce the clear not-found message.
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
  ranges) now guards the report week maths, the 7-day edit gate
  and the custom report range; src/barcode/encoder.h no longer
  repeats common.h's default arguments, so it can be included
  like any other header.
