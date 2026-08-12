# Raven CU Unlock

Date: 2026-08-12

Host: `192.168.1.221`

## Hardware

- GPU: AMD Raven Ridge / Radeon Vega Series
- PCI ID: `1002:15dd`
- Board subsystem: `1458:d000`
- GPU clock: 1100 MHz
- Default active CUs: 8
- Reported layout: 1 SE, 1 SH per SE, 11 CUs per SH
- VRAM: 256 MiB
- Kernel: `6.8.0-137-generic`
- Mesa/Rusticl: `25.2.8-0ubuntu0.24.04.2`

## Attribution

The idea and general methodology for this experiment were taken from
[`duggasco/bc250-40cu-unlock`](https://github.com/duggasco/bc250-40cu-unlock).
That project is the source of the guarded-init, controlled-count, benchmark,
and health-validation approach. The Raven register map and CU masks documented
here were derived separately for this GPU.

## Performance summary

Values below are from `clpeak` at 1100 MHz. The `float16` column is the
widest single-precision FP32 vector test, not half-precision arithmetic.

| Total CUs | Test mode | Unlock mask | FP32 `float` (GFLOPS) | FP32 `float16` (GFLOPS) | Result |
|---:|---|---|---:|---:|---|
| 8 | Stock driver | None | 1030-1040 | Not recorded | Baseline |
| 9 | Selective module, `raven_cu_unlock_cu=10` | One of bits 8-10 | 1121.60 | 1133.48 | Valid |
| 10 | Count module, `raven_cu_count=10` | `0x300` | 1268.09 | 1281.69 | Valid |
| 11 | Count module, `raven_cu_count=11` | `0x700` | 1325.36 | 1408.42 | Valid; full run completed |

The 10-CU run recorded the FP32 section only. The 11-CU run completed the
full AMD `clpeak` test and its other values are listed below.

## Valid 9-CU result

The valid 9-CU test used the early selective module during normal boot:

```text
/home/xrip/work/cu-single-test-20260812/amdgpu-selective.ko
```

Boot option:

```text
options amdgpu raven_cu_unlock_cu=10
```

The module was installed as:

```text
/lib/modules/6.8.0-137-generic/updates/extra/amdgpu.ko
```

The current standalone count patch can reproduce this target with
`raven_cu_count=9`.

The kernel reported:

```text
Raven CU test: enabling CU 10
SE 1, SH per SE 1, CU per SH 11, active_cu_number 9
```

No ring errors were present during this boot.

`clpeak` completed successfully:

```text
Single-precision compute (GFLOPS)
  float   : 1121.60
  float2  : 1174.07
  float4  : 1173.69
  float8  : 1157.64
  float16 : 1133.48
```

This was the valid baseline for the later count tests.

## Valid 10-CU result

The count module was tested during a normal boot with:

```text
options amdgpu raven_cu_count=10
Raven CU test: count 10, mask 0x300
SE 1, SH per SE 1, CU per SH 11, active_cu_number 10
```

The boot had no ring-test or GPU-reset errors. AMD Rusticl reported 10
compute units and `clpeak` completed successfully:

```text
Single-precision compute (GFLOPS)
  float   : 1268.09
  float2  : 1309.35
  float4  : 1300.60
  float8  : 1293.50
  float16 : 1281.69
```

## Valid 11-CU result

The same count module was tested with the complete Raven mask:

```text
options amdgpu raven_cu_count=11
Raven CU test: count 11, mask 0x700
SE 1, SH per SE 1, CU per SH 11, active_cu_number 11
```

The boot had no ring-test, reset, timeout, or GPU-fault messages. The full
AMD `clpeak` run was forced to Rusticl platform 1, device 0 and completed:

```text
Compute units   : 11
Clock frequency : 1100 MHz

Global memory bandwidth (GBPS)
  float   : 29.30
  float2  : 30.73
  float4  : 33.05
  float8  : 33.35
  float16 : 32.60

Single-precision compute (GFLOPS)
  float   : 1325.36
  float2  : 1382.54
  float4  : 1370.59
  float8  : 1363.60
  float16 : 1408.42

Half-precision compute (GFLOPS)
  half   : 1318.94
  half2  : 2502.57
  half4  : 1732.34
  half8  : 2044.00
  half16 : 2231.51

Integer compute (GIOPS)
  int   : 285.41
  int2  : 285.88
  int4  : 286.43
  int8  : 286.12
  int16 : 302.01

Integer compute Fast 24bit (GIOPS)
  int   : 1346.21
  int2  : 1360.51
  int4  : 1329.88
  int8  : 1356.29
  int16 : 1399.94

Transfer bandwidth (GBPS)
  enqueueWriteBuffer              : 13.70
  enqueueReadBuffer               : 12.64
  enqueueWriteBuffer non-blocking : 13.74
  enqueueReadBuffer non-blocking  : 12.67
  enqueueMapBuffer(for read)      : 2.81
  memcpy from mapped ptr          : 13.41
  enqueueUnmap(after write)       : 9.69
  memcpy to mapped ptr            : 12.34

Kernel launch latency : 148.71 us
```

The machine is currently left in this valid 11-CU mode. No benchmark process
is left running.

The current source change is in the standalone patch
`raven-gfx9-cu-unlock.patch`.

## Important parameter meaning

`raven_cu_unlock_cu` is a CU bit index. It is not the requested total CU count.

The stock Raven configuration has 8 active CUs and three inactive logical CU bits:

```text
bit 8  -> one extra CU -> 9 total CUs
bit 9  -> one extra CU -> 9 total CUs
bit 10 -> one extra CU -> 9 total CUs
```

Therefore, `raven_cu_unlock_cu=10` correctly produces 9 total CUs. It does not mean “enable 10 CUs.”

## Register details

The driver uses `CC_GC_SHADER_ARRAY_CONFIG` and reads the inactive-CU field with:

```text
INACTIVE_CUS mask  = 0xFFFF0000
INACTIVE_CUS shift = 0x10
```

The Raven driver limits the usable CU bitmap to 11 bits. The safe target range is therefore 8 through 11 total CUs. Bits 11 through 15 are outside the Raven CU layout and must not be treated as extra CUs.

## Failed or unsafe tests

- A patch that wrote zero to the whole inactive-CU field reported 11 CUs, but `gfx_low` and `gfx_high` IB tests timed out with `-110`.
- OpenCL then aborted with a rejected GPU command.
- Loading the module after removing the stock driver also caused ring errors. This is not equivalent to the valid boot-time test.
- These results are not accepted as valid performance measurements.

## Current conclusion

The boot-time count mask works for 9, 10, and 11 total CUs when it clears only
the real Raven hidden-CU bits. The previous crash was caused by the wider
all-zero register write, which also touched reserved bits 11 through 15.

The highest tested stable state is now 11 CUs. It passed the kernel checks,
reported 11 CUs through OpenCL, and completed the full AMD `clpeak` run.

Do not clear bits 11 through 15.
