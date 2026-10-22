# cr1-before bench and build procedures (T-B4-01)

Task: T-B4-01 (owner Ahsanul, support Benjamin). Target commit: `45e038ca2aa7d8386d5cb895838512d708e45b4d` (tag `cr1-before` once pushed).
Every value these steps produce goes into the "before" column of `docs/week13/before_after.md`. Until a step has run, its cell stays blank.

The T-B4-01 acceptance test (plan section 1) has three checks only: (1) the tag points at `45e038c`, (2) the first commit that adds `proto_put_bits`, a `proto_encode_` function or a `pack_` function descends from the tag, (3) `git grep` for `proto_put_bits`, `proto_encode`, `pack_frame` and `unpack` at the tag returns 0 lines. None of the steps below has a numeric pass in the T-B4-01 row. Where a step needs one, it is copied from the row it comes from (T-B3-01 for the heap probe) or labelled **(plan choice)** with the reason.

## Run order

| Step | What | Hardware | Evidence file |
|---|---|---|---|
| S-0 | Worktree at the tag | none | none |
| S-1 | `idf.py size` of `panel_sync` | none | `docs/week13/before/idf_size.txt` |
| S-2 | `sizeof` with the Xtensa compiler | none | `docs/week13/before/sizeof_xtensa.txt` |
| S-3 | `arm-none-eabi-size ledwall_sync.elf` | none | `docs/week13/before/pico_size.txt` |
| B-1 | Heap probe, 5 boots on each of 2 tiles | 2 tiles, router | `docs/week13/before/heap_at_tag.log` |
| B-2 | PC capture of the beacon broadcast, UDP port 4210 | Pico, 2 tiles, router, PC | `docs/week13/before/onair_before.pcapng`, `docs/week13/before/onair_before_beacon.txt` |
| B-3 | Pico log of `p->tot_len` for HELLO (`ledwall_sync/main.c:123`), run together with B-2 | same as B-2 | `docs/week13/before/onair_before_hello.log` |

Owner of every step: Ahsanul. Benjamin supports B-2 and B-3 (Pico).

## Does CI already give the sizes?

No. `.github/workflows/build.yml` at `45e038c`:

- Job `esp32` (container `espressif/idf:v5.3`) runs `idf.py set-target esp32s3` and `idf.py build` for `panel_sync` and `boardtest` (lines 25-37) and uploads only `panel_sync/build/panel_sync.bin` (lines 39-42). It never runs `idf.py size`. The build log ends with ESP-IDF's application binary size check against the 0x180000 factory partition, which is the `.bin` size, not the DRAM/IRAM breakdown that `idf.py size` gives.
- Job `pico` builds `ledwall_sync` with `cmake ..` and `make` against Pico SDK 2.3.1 (lines 56-69) and uploads only `ledwall_sync.uf2` (lines 71-74). It never runs `arm-none-eabi-size`, and the `.elf` is not uploaded, so the size cannot be taken from a CI artefact either.

So the sizes come from S-1 and S-3 below (local, preferred). `build.yml` is not changed on any branch.

### Optional: the same output from CI on a throwaway branch

Only if a local toolchain is not available. Add these steps on a throwaway branch made from the tag (the firmware sources stay those of the tag; only the workflow changes), push it, download the two artefacts from the Actions run, then delete the branch. Do not merge it.

In job `esp32`, after the step "Build panel_sync":

```yaml
      - name: Size panel_sync (T-B4-01, throwaway)
        run: |
          . "$IDF_PATH/export.sh"
          cd panel_sync
          idf.py --version | tee idf_size.txt
          idf.py size | tee -a idf_size.txt

      - uses: actions/upload-artifact@v4
        with:
          name: idf_size
          path: panel_sync/idf_size.txt
```

In job `pico`, after the step "Build ledwall_sync":

```yaml
      - name: Size ledwall_sync (T-B4-01, throwaway)
        run: |
          arm-none-eabi-gcc --version | head -1 | tee ledwall_sync/build/pico_size.txt
          arm-none-eabi-size ledwall_sync/build/ledwall_sync.elf | tee -a ledwall_sync/build/pico_size.txt

      - uses: actions/upload-artifact@v4
        with:
          name: pico_size
          path: ledwall_sync/build/pico_size.txt
```

Caveat: the CI Pico job takes `gcc-arm-none-eabi` from Ubuntu's archive, so its compiler version can differ from the local one; the header line records it.

## S-0: worktree at the tag (no hardware)

In WSL, in your clone of `Led_Matrix`:

```bash
export REPO=~/esp-projects/Led_Matrix          # your clone; change if it lives elsewhere
export BUILD=~/esp-projects/cr1-before-build   # separate worktree, never committed from
export EV=$REPO/docs/week13/before
mkdir -p ~/cr1-logs                            # raw serial logs, outside the repo

cd $REPO
git fetch origin --tags
git checkout b4/cr1-baseline
git rev-list -n 1 cr1-before                   # must print 45e038ca2aa7d8386d5cb895838512d708e45b4d
git worktree add --detach $BUILD cr1-before
git -C $BUILD status --short                   # must print nothing
```

If the tag is not pushed yet, use `45e038c` in place of `cr1-before`.

## S-1: `idf.py size` of panel_sync (no hardware)

Owner: Ahsanul. ESP-IDF v5.3, target esp32s3.

```bash
. $HOME/esp/esp-idf/export.sh                  # same as get_idf
cd $BUILD
cp panel_sync/main/wifi_secrets.example.h panel_sync/main/wifi_secrets.h   # placeholder credentials, as CI does
cd panel_sync
idf.py set-target esp32s3
idf.py build
{ echo "# idf.py size, panel_sync at $(git -C $BUILD rev-parse HEAD) (cr1-before), target esp32s3, placeholder wifi_secrets.h as in CI"
  echo "# $(idf.py --version)"
  idf.py size; } > $EV/idf_size.txt
```

- Plan definition (T-B4-01 row): "`docs/week13/before/idf_size.txt` (`idf.py size` of `panel_sync` built at the tag)"; the before cell is "static DRAM/IRAM from `idf.py size`". No numeric pass.
- Done when: the file holds the commit hash `45e038ca...`, `ESP-IDF v5.3` and the `idf.py size` memory table (DRAM and IRAM lines). Copy the DRAM and IRAM "used" figures into `before_after.md`.
- Placeholder credentials **(plan choice)**: reproducible by anyone and identical to the CI build. Real credentials change only string constants, which `idf.py size` reports under flash rodata, not DRAM or IRAM.

## S-2: sizeof with the target compiler (no hardware)

Owner: Ahsanul. Needs the ESP-IDF environment from S-1.

```bash
cd $REPO
xtensa-esp32s3-elf-gcc -c -o /tmp/sizeof_xtensa.o docs/week13/before/sizeof.c
{ echo "# sizeof(beacon_t) and sizeof(hello_t) from the Xtensa compiler, structs copied verbatim from panel_sync.c:52-69 at 45e038c"
  echo "# $(xtensa-esp32s3-elf-gcc --version | head -1)"
  echo "# nm -S -t d: the second column is the symbol size, which equals sizeof() on the target"
  xtensa-esp32s3-elf-nm -S -t d /tmp/sizeof_xtensa.o | grep sizeof_; } > $EV/sizeof_xtensa.txt
cat $EV/sizeof_xtensa.txt
```

If `xtensa-esp32s3-elf-gcc` is not on the path, take the compiler path from `grep CMAKE_C_COMPILER: $BUILD/panel_sync/build/CMakeCache.txt` and use the matching `nm` from the same folder.

- Pass criterion (T-B4-01 row): "field sum from `panel_sync.c:52-63` and `panel_sync.c:65-69` is 50 B and 9 B **(derived)**; the print confirms". Pass = the size column reads 50 for `sizeof_beacon_t` and 9 for `sizeof_hello_t`. Any other value is recorded as measured, not corrected.
- Why a compiler check stands in for a print on the tile: `sizeof` is fixed by the compiler at build time, so the ESP-IDF compiler's object file holds the target value without flashing. See the header of `sizeof.txt` for why host and target agree by construction (packed attribute, exact-width types).

## S-3: arm-none-eabi-size of ledwall_sync (no hardware)

Owner: Ahsanul. Pico SDK 2.3.1.

```bash
export PICO_SDK_PATH=$HOME/pico/pico-sdk
git -C $PICO_SDK_PATH describe --tags          # must print 2.3.1
cd $BUILD
cp ledwall_sync/wifi_secrets.example.h ledwall_sync/wifi_secrets.h       # placeholder credentials, as CI does
mkdir -p ledwall_sync/build && cd ledwall_sync/build
cmake .. && make -j4
{ echo "# arm-none-eabi-size, ledwall_sync at $(git -C $BUILD rev-parse HEAD) (cr1-before), Pico SDK $(git -C $PICO_SDK_PATH describe --tags), placeholder wifi_secrets.h as in CI"
  echo "# $(arm-none-eabi-gcc --version | head -1)"
  arm-none-eabi-size ledwall_sync.elf; } > $EV/pico_size.txt
```

- Plan definition (T-B4-01 row): "`docs/week13/before/pico_size.txt` (`arm-none-eabi-size ledwall_sync.elf`)". No numeric pass.
- Done when: the file holds the commit hash, `2.3.1` and one `text data bss dec hex filename` line for `ledwall_sync.elf`.

## B-1: heap at the tag, `heap_at_tag.log` (2 tiles)

Owner: Ahsanul (the probe itself is Cheston's T-B3-01; this step applies it unchanged to the tag).

The probe is the T-B3-01 one: `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)`, `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` and `heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)`, logged at the plan's hook `panel_sync.c:93-96` (the `IP_EVENT_STA_GOT_IP` branch of `wifi_event`). It is applied to a local build of the tag and never committed. The change is in `docs/week13/before/heap_probe.patch`:

```c
#include "esp_heap_caps.h"   /* T-B3-01 probe, local only */      /* added after line 20, #include "esp_mac.h" */

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "joined %s, IP " IPSTR, WIFI_SSID, IP2STR(&e->ip_info.ip));
        /* T-B3-01 probe, local only: never commit */
        ESP_LOGI(TAG, "HEAP largest_internal=%u free_internal=%u largest_spiram=%u",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    }
```

The `#include` is part of the probe: it declares the three `heap_caps` functions. Nothing else changes.

Preconditions:
- Each tile has its board number (`./set_board_number.sh <n>`), and the router is up.
- The prototype runs as the plan describes it: "whole-GIF animation mapped from flash, `frames.c:66-71`, at 30 fps". The boot line `GIF: ... (partition)` shows the mapped path. If it says `using built-in animation`, write an asset first with `tools/upload_frames.sh <gif>` from `$BUILD/panel_sync` and note the GIF name in the log header **(plan choice: any GIF; the plan does not name one)**.
- The Pico runs the plain tag build, so the tiles run as in normal use **(plan choice)**.
- Attach each tile with `usbipd attach --wsl --busid <BUSID> --auto-attach` (PowerShell, Windows) so the port comes back after every reset.

Apply, build, run:

```bash
cd $BUILD
cp panel_sync/main/wifi_secrets.example.h panel_sync/main/wifi_secrets.h
nano panel_sync/main/wifi_secrets.h            # real ROUTER_SSID and ROUTER_PASS (git-ignored, not a tracked change)
git apply $EV/heap_probe.patch
git -C $BUILD diff --stat                      # must show only: panel_sync/main/panel_sync.c | 6 ++++++
cd panel_sync && idf.py build

# tile 1 (repeat with heap_raw_tile2.log for tile 2)
idf.py -p /dev/ttyACM0 flash
script -q -c "idf.py -p /dev/ttyACM0 monitor" ~/cr1-logs/heap_raw_tile1.log
#   boot 1 is the reset when the monitor opens; wait for the "HEAP largest_internal=" line,
#   then Ctrl+T Ctrl+R to reset; repeat until 5 HEAP lines have appeared; leave with Ctrl+]
```

Assemble the log and check it:

```bash
{ echo "# heap_at_tag.log: T-B3-01 probe on a local, uncommitted build of cr1-before ($(git -C $BUILD rev-parse HEAD))"
  echo "# $(idf.py --version); 5 boots on each of 2 tiles; the only change is the diff below"
  git -C $BUILD diff
  for t in 1 2; do
    sed 's/\x1b\[[0-9;]*m//g' ~/cr1-logs/heap_raw_tile$t.log \
      | grep -a -E "I am ESP-|GIF: |HEAP largest_internal" | sed "s/^/tile$t: /"
  done; } > $EV/heap_at_tag.log
grep -c "HEAP largest_internal=" $EV/heap_at_tag.log      # must print 10
```

Revert, then put the plain tag back on both tiles:

```bash
git -C $BUILD apply -R $EV/heap_probe.patch
git -C $BUILD diff --stat                      # must print nothing
cd $BUILD/panel_sync && idf.py build && idf.py -p /dev/ttyACM0 flash   # each tile
```

- Pass criterion (copied from T-B3-01, whose probe this is): "5 boots on each of 2 tiles **(plan choice)**: 10 of 10 boots log all three values".
- Before cells: the minimum over the 10 boots of `free_internal` and of `largest_internal` **(plan choice: T-B3-01 decides OD-04 from the minimum internal value, so the before and after cells use the same statistic)**. `largest_spiram` is recorded in the log only; PSRAM is not enabled in `panel_sync/sdkconfig.defaults` at the tag, and the value is taken as printed.
- The plan's hook comment for T-B3-01 asks for the probe "once all tile tasks exist". At the tag the render task is created before `wifi_init()` and `sync_rx` and `hello` straight after it (`panel_sync.c:241-245`); `GOT_IP` normally arrives after DHCP, later than those calls, but the log does not prove the order. This is the hook the plan names, so it is used as is.

## B-2: on-air beacon bytes, `onair_before.pcapng` (Pico, 2 tiles, PC)

Owner: Ahsanul, support Benjamin. Run together with B-3: the Pico runs the tag plus `hello_len.patch` (a log line inside the HELLO receive callback only; the beacon path at `main.c:259-288` is untouched), the tiles run the plain tag.

The PC must be on the same router as the Pico: a cable into a router LAN port (simplest; the router forwards the subnet broadcast to wired clients) or the 2.4 GHz WiFi with power save off. Capture on Windows, not inside WSL2, whose virtual network does not see LAN broadcasts. Wireshark with Npcap installed.

In PowerShell:

```powershell
& "C:\Program Files\Wireshark\tshark.exe" -D        # note the number of the Ethernet or Wi-Fi interface
& "C:\Program Files\Wireshark\tshark.exe" -i <n> -f "udp dst port 4210" -c 100 -w "$env:USERPROFILE\onair_before.pcapng"
& "C:\Program Files\Wireshark\tshark.exe" -r "$env:USERPROFILE\onair_before.pcapng" -T fields -E header=y `
    -e frame.number -e frame.time_relative -e ip.src -e ip.dst -e udp.length -e data.len `
  | Out-File -Encoding ascii "$env:USERPROFILE\onair_before_beacon.txt"
```

- Capture filter: `udp dst port 4210` (BPF). Display filter for Wireshark: `udp.dstport == 4210`.
- `udp.length` is header plus payload; the UDP payload is `udp.length - 8`, which `data.len` also shows.
- 100 beacons (10 s at the 10 Hz beacon rate) **(plan choice: enough to show that every beacon has the same length)**.

Copy into the repo (WSL):

```bash
cp /mnt/c/Users/<you>/onair_before.pcapng /mnt/c/Users/<you>/onair_before_beacon.txt $EV/
tail -n +2 $EV/onair_before_beacon.txt | cut -f3,5,6 | sort | uniq -c   # one line: 100, the Pico IP, one udp.length, one data.len
```

- Plan definition (T-B4-01 row): "PC capture of the prototype beacon broadcast on port 4210"; the before cell is the "on-air UDP payload bytes of the beacon". No numeric pass.
- Done when **(plan choice)**: 100 of 100 captured beacons come from the Pico's IP with one payload length, and that length goes into `before_after.md`. If it differs from the 50 B field sum, record the measured value and note the difference; do not change it.

## B-3: HELLO length on the Pico, `onair_before_hello.log` (same run as B-2)

Owner: Ahsanul, support Benjamin.

HELLO is unicast from each tile to the Pico on port 4211, so a PC on the router does not see it. The Pico logs `p->tot_len` (the UDP payload length lwIP hands to the receive callback) at `ledwall_sync/main.c:123`. The change is in `docs/week13/before/hello_len.patch`, one line after `hello_t h;` (line 122):

```c
    printf("HELLO tot_len=%u from %s\n", p->tot_len, ipaddr_ntoa(addr));   /* T-B4-01 log, local only */
```

The callback already calls `printf` at `main.c:131`, `142` and `150`, so this adds no new kind of call in that context.

```bash
cd $BUILD
cp ledwall_sync/wifi_secrets.example.h ledwall_sync/wifi_secrets.h
nano ledwall_sync/wifi_secrets.h               # real ROUTER_SSID and ROUTER_PASS (git-ignored)
git apply $EV/hello_len.patch
git -C $BUILD diff --stat                      # must show only: ledwall_sync/main.c | 1 +
cd ledwall_sync/build && cmake .. && make -j4
# hold BOOTSEL, plug in the Pico, copy ledwall_sync.uf2 onto RPI-RP2
#   (Windows path: \\wsl$\Ubuntu\home\<you>\esp-projects\cr1-before-build\ledwall_sync\build\ledwall_sync.uf2)
# attach the Pico's USB serial to WSL with usbipd; it appears as /dev/ttyACM<k>
stty -F /dev/ttyACM1 raw                       # use the Pico's port
timeout 60 cat /dev/ttyACM1 > ~/cr1-logs/pico_raw.log      # start B-2's tshark capture during this minute
{ echo "# Pico log of p->tot_len for HELLO (ledwall_sync/main.c:123), cr1-before ($(git -C $BUILD rev-parse HEAD)) plus the diff below only"
  git -C $BUILD diff
  grep -a "HELLO tot_len=" ~/cr1-logs/pico_raw.log | head -n 100; } > $EV/onair_before_hello.log
grep -c "HELLO tot_len=" $EV/onair_before_hello.log                     # must print 100
grep -a "HELLO tot_len=" $EV/onair_before_hello.log | awk '{print $2, $4}' | sort | uniq -c   # one tot_len, both tile IPs
```

Revert, then put the plain tag back on the Pico:

```bash
git -C $BUILD apply -R $EV/hello_len.patch
git -C $BUILD diff --stat                      # must print nothing
cd $BUILD/ledwall_sync/build && make -j4       # copy ledwall_sync.uf2 onto RPI-RP2 again
```

- Plan definition (T-B4-01 row): "the Pico log of `p->tot_len` for HELLO (`main.c:123`)"; the before cell is the "on-air UDP payload bytes of the ... HELLO". No numeric pass.
- Done when **(plan choice)**: 100 HELLO lines, from both tile IPs, with one `tot_len` value; that value goes into `before_after.md`. 100 lines is about 15 s with 2 tiles sending every 0.3 s (`panel_sync.c:161`).

## After the runs

```bash
cd $REPO
git add docs/week13/before/idf_size.txt docs/week13/before/sizeof_xtensa.txt docs/week13/before/pico_size.txt \
        docs/week13/before/heap_at_tag.log docs/week13/before/onair_before.pcapng \
        docs/week13/before/onair_before_beacon.txt docs/week13/before/onair_before_hello.log
# fill the blank "before" cells in docs/week13/before_after.md from these files, then:
git add docs/week13/before_after.md
git commit -m "Add cr1-before measured sizes, heap log and captures"
git worktree remove --force $BUILD
```
