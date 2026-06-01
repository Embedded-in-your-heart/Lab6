# Lab6 — DFSDM PDM 麥克風 DMA 擷取最小重現專案

本專案是一個**獨立的最小重現（minimal reproduction）／驗證平台**，專門為了解決下列 GitHub issue 而開：

> **[#1 M3a blocker: DFSDM1_FLT0 DMA request mapping wrong on STM32L475](https://github.com/Embedded-in-your-heart/Intelligent-home-STM32-client/issues/1)**

主專案（`Intelligent-home-STM32-client`）的 Milestone 3a 無法擷取 PDM 麥克風音訊：
`HAL_DFSDM_FilterRegularStart_DMA()` 回傳 `HAL_OK`，但 DMA 的 half / full 完成回呼**永遠不會觸發**，`CNDTR` 也停在初始值不動。為了在不受主專案大量 BLE / 感測器程式碼干擾的環境下定位並修復這個問題，特別把 DFSDM + DMA + FreeRTOS 抽出來成為本專案。

---

## 目標

讓 B-L475E-IOT01A 開發板上的 **MP34DT01 MEMS 數位麥克風**，能透過 **DFSDM1 + DMA** 連續地把 PDM 資料解調並搬進記憶體緩衝區，並穩定觸發以下回呼：

- `HAL_DFSDM_FilterRegConvHalfCpltCallback()`（半滿）
- `HAL_DFSDM_FilterRegConvCpltCallback()`（全滿）

> **目前狀態（分支 `feature/turtle-option1`）**：DFSDM 的 DMA 回呼仍未觸發。經完整診斷後，問題已定位到 **DMA1 忽略周邊 DMA request**（非 DFSDM 專屬、非設定錯誤），詳見下方「[診斷結論](#診斷結論diagnostic-findings)」。
>
> **可用解法**：**Polling 模式**（`HAL_DFSDM_FilterRegularStart` + `HAL_DFSDM_FilterPollForRegConversion` / `HAL_DFSDM_FilterGetRegularValue`）已驗證可正常讀到麥克風數據，足以讓 `MicLevel` / `LoudAlert` 先上線。

### Issue 進展與更正（2026-05）

> ⚠️ **原始 issue 的根因判斷有誤，已更正**（見 [issue #1 留言](https://github.com/Embedded-in-your-heart/Intelligent-home-STM32-client/issues/1)）。

最初 issue 推測「CubeMX 把 DMA request 設成 `DMA_REQUEST_0`（誤以為是 ADC2），應改成 7」。但這是**查錯表**——STM32L475 的 DMA1 request 對應在 **RM0351 Rev 9 _Table 44_**（不是 Table 41）：

| `CxS[3:0]` | Ch1 | Ch2 | Ch3 | **Ch4** | Ch5 | Ch6 | Ch7 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0000` | ADC1 | ADC2 | ADC3 | **DFSDM1_FLT0** | DFSDM1_FLT1 | DFSDM1_FLT2 | DFSDM1_FLT3 |

即 **DMA1_Channel4 + `C4S=0`（`DMA_REQUEST_0`）就是 DFSDM1_FLT0**——正是 CubeMX 生成的值（`stm32l4xx_hal_msp.c`），也與 ST 官方 BSP `stm32l475e_iot01_audio.c`（`DMA1_Channel4` + `DMA_REQUEST_0`）一致。

**結論：**
- `.ioc` / 生成 MSP 的 DMA request 對應**正確，不是 bug**。
- 「強制 C4S=7」是**反方向**（Ch4 的 request 7 並非 DFSDM）；「寫 7 後 C4S 仍是 0」正是 HAL 把它修回正確值 0 的表現。
- 真正的失效點仍在 **DMA 搬移路徑**，但**與 request mux 無關**。Polling 可正常讀資料即佐證 mic / DFSDM / 時脈 / PDM 解調都沒問題。

---

## 診斷結論（Diagnostic Findings）

> 對應 [issue #1 的診斷留言](https://github.com/Embedded-in-your-heart/Intelligent-home-STM32-client/issues/1)。下方「除錯計畫」是方法論與重現步驟；本節是執行後的**結論**。

### 一句話結論

**DMA1 能服務軟體觸發（MEM2MEM）的搬移，卻忽略所有周邊 DMA request——ADC（ch1）與 DFSDM（ch4）兩個獨立通道都一樣，且設定全部正確、無任何錯誤旗標。** 由於 ST 官方 BSP 能在同一塊板子上跑通 DFSDM+DMA，這不是晶片損壞，而是指向**環境／版本差異**（CubeMX 6.17.0 / FW_L4 V1.18.2 生成碼、CMake 專案），而非看得見的設定錯誤。

### 證據彙整

| 環節 | 結果 | 證據 |
| --- | --- | --- |
| DMA request 對應 | ✅ 正確 | 執行期 `CSELR.C4S=0`（= DFSDM1_FLT0，RM0351 Table 44） |
| 麥克風 / DFSDM / 時脈 / PDM | ✅ 正常 | Polling 讀得到有效資料 |
| 記憶體存取 + DMA 控制器 | ✅ 正常 | M2M SRAM→SRAM：`poll=0 err=0x0 CNDTR=0 dst==src` |
| DFSDM 周邊觸發 DMA | ❌ 不搬 | `ROVRF=1`（有產出且 overrun）但 `CNDTR=1024` 不動 |
| ADC 周邊觸發 DMA（對照組） | ❌ 不搬 | `EOC=1`（轉換完成）卡住、`CNDTR=8` 不動 |
| 中斷 / LINKDMA / NVIC 接線 | ✅ 正確 | 已逐一核對生成碼 |
| HAL 流程 | ✅ 正確 | `CPAR=&FLTRDATAR` 後 `RSWSTART`，與 ST BSP 一致 |
| 有無干擾性程式碼 | ✅ 無 | `Core/` 內無 `SYSCFG`/`DBGMCU`/`CSELR`/DMA-disable 寫入 |
| FreeRTOS | ✅ 已排除 | 所有失敗都在 `osKernelInitialize()` 之前（純 HAL 階段） |

### 關鍵 log

```
[diag:m2m] init=0 start=0 poll=0 err=0x0 CNDTR=0  dst[0]=A5A50000 dst[7]=A5A50007 (src[0]=A5A50000)
[diag:after-300ms]
  DMA  CSELR=00000000 C4S=0          (正確 = DFSDM1_FLT0)
  DMA  CCR=00000AAF EN=1 HTIE=1 TCIE=1 CIRC=1  CNDTR=1024   <- 從不遞減
  FLT0 CR1=02240001 RDMAEN=1  ISR=00FF400A REOCF=1 ROVRF=1  <- 有產出 + overrun
  ADC  CNDTR=8 (DMA1_Ch1 ref) cplt=0
  SYS  DMA1_ISR=00000000  ADC_ISR=0000000F EOC=1 OVR=0       <- 無任何 DMA 錯誤旗標
```

### 已排除的假設

- ❌ DMA request 對應錯誤（原 issue 理論）→ `C4S=0` 是正確值
- ❌ buffer 在 DMA 碰不到的記憶體（H7 的 DTCM 解法）→ L475 無 DTCM，buffer 在 SRAM1，M2M 已證實 DMA1 可存取
- ❌ FreeRTOS 中斷遮蔽 → 失敗早於 scheduler，且 `CNDTR` 是硬體計數與中斷無關
- ❌ NVIC / IRQHandler / LINKDMA 接線錯誤 → 已核對正確

### 下一步

1. **隔離「程式 vs 環境」**：在同一塊板子燒錄 ST 已知可動的參考（**X-CUBE-MEMSMIC** 麥克風範例，或 BSP `BSP_AUDIO_IN`）。會動 → 逐行 diff 其 DFSDM/DMA 初始化；也不動 → 帶此重現上報 ST。
2. **產品先解鎖**：用 **Polling**（已驗證）讓 `MicLevel` / `LoudAlert` 上線，DFSDM-DMA 當獨立議題追。

---

## 除錯計畫（Debugging Plan）

由於 **Polling 已驗證可讀到麥克風數據**，麥克風／DFSDM／時脈皆排除，問題鎖定在 **DMA 搬移 + 中斷回呼** 這條路徑。以下以「**由近而遠、逐層隔離**」的階梯式測試進行，每步都有預期結果與分支判斷，避免亂槍打鳥。

> 所有測試碼請寫在 `Core/Src/main.c` 的 `/* USER CODE BEGIN ... END */` 區塊內（符合 [`.claude/rules`](./.claude/rules/file-modification-scope)）。若需改 `.ioc` 設定，先告知維護者重新 generate。

### Phase A — 確認問題範圍 ✅（已完成）

| 測試 | 結果 | 結論 |
| --- | --- | --- |
| Polling 讀麥克風 | ✅ 成功 | 麥克風、DFSDM filter、時脈、PDM 解調正常 |

→ 問題在 **DMA 路徑**，進入 Phase B。

### Phase B — 隔離「DMA 中斷／回呼基礎建設」

目的：先確認「DMA 控制器 → NVIC → IRQHandler → HAL 回呼」這條鏈路本身會不會動，**與 DFSDM 完全脫鉤**。

#### B1. 偽資料 Memory-to-Memory DMA 測試（建議優先做）

用一段**假的記憶體緩衝區**做 M2M 搬移。M2M 由軟體驅動、**不經過 request mux（CSELR）**，因此能乾淨地單獨驗證中斷 plumbing。直接複用已接好 NVIC 與 IRQHandler 的 `DMA1_Channel4`（即 `hdma_dfsdm1_flt0`）：

```c
/* USER CODE BEGIN PV */
static uint32_t srcBuf[16] = { /* 0,1,2,... 任意非零測試樣式 */ };
static uint32_t dstBuf[16];
volatile uint8_t m2mDone = 0;
/* USER CODE END PV */

/* USER CODE BEGIN 4 */
static void M2M_TC_cb(DMA_HandleTypeDef *h) { m2mDone = 1; }  /* DMA 原生 TC 回呼 */
/* USER CODE END 4 */

/* 在 MX_DFSDM1_Init() 之後、HAL_DFSDM_FilterRegularStart_DMA() 之前（USER CODE BEGIN 2）：*/
hdma_dfsdm1_flt0.Init.Direction   = DMA_MEMORY_TO_MEMORY;
hdma_dfsdm1_flt0.Init.PeriphInc   = DMA_PINC_ENABLE;
hdma_dfsdm1_flt0.Init.MemInc      = DMA_MINC_ENABLE;
hdma_dfsdm1_flt0.Init.Mode        = DMA_NORMAL;
HAL_DMA_Init(&hdma_dfsdm1_flt0);
HAL_DMA_RegisterCallback(&hdma_dfsdm1_flt0, HAL_DMA_XFER_CPLT_CB_ID, M2M_TC_cb);
HAL_DMA_Start_IT(&hdma_dfsdm1_flt0, (uint32_t)srcBuf, (uint32_t)dstBuf, 16);
/* 之後在任務裡輪詢 m2mDone 並印出 */
```

| 結果 | 解讀 | 下一步 |
| --- | --- | --- |
| `m2mDone` 變 1、`dstBuf` == `srcBuf` | DMA 控制器 + Ch4 NVIC + IRQHandler + HAL 回呼派發**全部正常** | 問題在「DFSDM 是否真的對 DMA 發 request」（`RDMAEN` / 啟動順序）→ 跳 Phase C |
| `m2mDone` 永遠 0 | 中斷 plumbing 本身壞了（NVIC priority/mask、IRQHandler 沒呼叫 `HAL_DMA_IRQHandler`、或被 RTOS critical section 遮蔽） | 先修 Phase B 的 plumbing（見 C2 檢查項） |

> 註：此處用的是 DMA **原生** TC 回呼，與 `HAL_DFSDM_FilterRegConvCpltCallback`（由 HAL_DFSDM 內部轉發）不同；先確認底層通了，再回到 DFSDM 包裝層。測完記得把 `hdma_dfsdm1_flt0` 還原回 `DMA_PERIPH_TO_MEMORY` / `DMA_CIRCULAR`。

#### B2. 既有 ADC + DMA 路徑做對照

本專案已配置 **ADC1 + DMA1_Channel1**（TIM1 觸發、circular），且 ADC1 在 Channel1 上的 request 對應是**正確的**。拿它當「正常 DMA 周邊」對照組：

```c
HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adcBuf, 1);
HAL_TIM_Base_Start(&htim1);   /* 提供 TRGO 觸發 */
/* 實作 HAL_ADC_ConvCpltCallback() 內 osSemaphoreRelease(adcSemHandle) 或設旗標 */
```

| 結果 | 解讀 |
| --- | --- |
| ADC 的 `HAL_ADC_ConvCpltCallback` 會觸發 | DMA + NVIC 整體健康，**問題確定只在 DFSDM/Channel4 的 request 對應** |
| ADC DMA 也不觸發 | 與 B1 互相佐證 plumbing 層級問題 |

### Phase C — request 對應已排除，深入 DMA 搬移失效點

> 📌 **重要更正**：經 RM0351 **Table 44** 確認，`C4S=0` 是 DFSDM1_FLT0 的**正確**值（見上方「Issue 進展與更正」）。因此 **不要再嘗試強制 C4S=7**——那是原 issue 的誤判方向。本階段改為在 request 已正確的前提下，找出 DMA 為何不搬資料。

#### C1. 在本最小專案重新採集診斷（不要沿用主專案舊數據）

在 `HAL_DFSDM_FilterRegularStart_DMA()` 之後 dump：

```c
printf("CSELR=%08lX (C4S=%lu)\r\n", DMA1_CSELR->CSELR, (DMA1_CSELR->CSELR >> 12) & 0xF);
printf("CCR=%08lX CNDTR=%lu\r\n", hdma_dfsdm1_flt0.Instance->CCR, hdma_dfsdm1_flt0.Instance->CNDTR);
printf("FLT0 CR1=%08lX ISR=%08lX\r\n", hdfsdm1_filter0.Instance->FLTCR1, hdfsdm1_filter0.Instance->FLTISR);
```

| 暫存器 / 位元 | 期望（正常） | 異常代表 |
| --- | --- | --- |
| `DMA1_CSELR.C4S`（bit 15:12） | **`0`（= DFSDM1_FLT0，正確）** | 非 0 才有問題 |
| `FLTCR1.RDMAEN`（bit 21） | `1` | `0` → DFSDM 沒在發 DMA request |
| `DMA CCR.EN/HTIE/TCIE/CIRC` | 全 `1` | 缺 → DMA 沒 arm 或沒開中斷 |
| `CNDTR` | 隨資料遞減 | 卡住 → DMA 沒搬 |
| `FLTISR.ROVRF`（regular overrun） | `0` | `1` → filter 有出資料但沒被 DMA 取走 |

> 若 `C4S=0`、`RDMAEN=1`、`CCR` 都正確、但 `CNDTR` 仍卡住且 `ROVRF=1`：代表 **DMA 控制器收不到（或不處理）DFSDM 的 request**，往 C2 / C3 找。

#### C2. 檢查中斷與 LINKDMA 接線（已核對皆正確，留作回歸檢查）

- `Core/Src/stm32l4xx_it.c`：`DMA1_Channel4_IRQHandler()` 確實呼叫 `HAL_DMA_IRQHandler(&hdma_dfsdm1_flt0)` ✅
- `Core/Src/stm32l4xx_hal_msp.c`：`HAL_DFSDM_FilterMspInit` 內有 `__HAL_LINKDMA(hdfsdm_filter, hdmaReg / hdmaInj, hdma_dfsdm1_flt0)` ✅
- NVIC：`DMA1_Channel4_IRQn` priority（5）需 ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`（FreeRTOS 才不會遮蔽）✅

#### C3. 真正可疑的方向（取代原「強制 C4S=7」）

- **先做 Phase B 的 M2M 測試**：若 M2M 在 Ch4 上能觸發回呼 → DMA 控制器與中斷完全正常，問題只剩「DFSDM 是否真的對 DMA 發 request」（即 `RDMAEN` 與啟動順序）。
- 確認 `HAL_DFSDM_FilterRegularStart_DMA` 的**呼叫時機**：本專案在 `osKernelStart()` 之前就呼叫（`main` 的 `USER CODE 2`），確認此時 DFSDM 時脈／channel 已就緒。
- 拿 **polling 當「已知正常」基準**：polling 會動、DMA 不會動，逐一比對兩條路徑下的 `FLTCR1`（特別是 `RDMAEN`）與 DMA `CCR`，差異處就是線索。
- 排除 RTOS 變因：必要時先在**裸機**（不啟動 scheduler）下單測 DMA，排除 FreeRTOS 中斷遮蔽 / 時序問題。

| 結果 | 解讀 |
| --- | --- |
| `CNDTR` 開始遞減、回呼觸發 | DMA 路徑修復 → 收斂 issue #1 |
| 仍卡住 | 依 C1 表格定位是 `RDMAEN`、`CCR`、還是啟動順序問題，逐項排除 |

### Phase D — Fallback：Polling 模式

若 DMA 路徑最終無法在時程內修好，**Polling 已驗證可用**，可作為功能保底：

```c
HAL_DFSDM_FilterRegularStart(&hdfsdm1_filter0);
/* 任務迴圈內： */
HAL_DFSDM_FilterPollForRegConversion(&hdfsdm1_filter0, timeout);
int32_t v = HAL_DFSDM_FilterGetRegularValue(&hdfsdm1_filter0, &ch);
```

代價：CPU 需主動輪詢、吞吐較低，但可讓 `MicLevel` / `LoudAlert` 等功能先上線（對應 issue 的 disposition）。

### 快速判斷流程

```
Polling OK ──► 問題在 DMA（request 對應已確認正確，C4S=0）
        │
        ├─ B1 偽資料 M2M ──┬─ 觸發 ──► C：查 RDMAEN / 啟動順序 / RTOS 變因
        │                  └─ 不觸發 ─► 修 plumbing（NVIC / IRQHandler / LINKDMA）
        │
        └─ B2 ADC 對照 ──► 佐證 DMA 整體健康
                                 │
                                 └─ 全失敗 / 時程不足 ──► Phase D：Polling 保底
```

---

## 硬體環境

| 項目 | 內容 |
| --- | --- |
| 開發板 | **B-L475E-IOT01A1** |
| MCU | **STM32L475VGT6**（Cortex-M4, 80 MHz, LQFP100） |
| 麥克風 | MP34DT01-M PDM MEMS 麥克風（板載） |
| Firmware Package | STM32Cube FW_L4 V1.18.2 |
| CubeMX 版本 | 6.17.0 |

### 關鍵腳位

| Pin | 訊號 | 用途 |
| --- | --- | --- |
| PE7 | `DFSDM1_DATIN2` | 麥克風 PDM 資料輸入（MP34DT01_DOUT） |
| PE9 | `DFSDM1_CKOUT` | 麥克風時脈輸出（MP34DT01_CLK） |
| PB6 / PB7 | `USART1_TX / RX` | ST-LINK 虛擬序列埠（printf 除錯輸出） |

---

## 啟用的周邊與設定

所有設定皆來自 [`Lab6.ioc`](./Lab6.ioc)（請以 CubeMX 開啟修改，勿手改原始碼 — 見下方規則）。

### DFSDM1（PDM 解調，本專案重點）
- Filter0 + Channel2，外部序列輸入、內部時脈
- SINC3 filter，FOSR = 250、Divider = 40
- 輸出時脈來源：`DFSDM_CHANNEL_OUTPUT_CLOCK_AUDIO`
- Regular channel 連續轉換、**DMA 模式啟用**
- DMA：**DMA1_Channel4**，Peripheral→Memory，Word 對齊，**Circular**

### ADC1（溫度感測器，輔助／對照用）
- Channel = 內部 `TEMPSENSOR`，12-bit，取樣 640.5 cycles
- 外部觸發：**TIM1 TRGO**（上升緣）
- DMA：**DMA1_Channel1**，Half-word，Circular，搭配 binary semaphore `adcSem`

### Timers
| Timer | Prescaler / Period | 用途 |
| --- | --- | --- |
| TIM1 | 80-1 / 10000-1 | TRGO 觸發 ADC（1 Hz） |
| TIM2 | 80-1 / 1000000-1 | TRGO（備用） |
| TIM6 | — | 一般計時 |
| TIM7 | — | FreeRTOS / HAL time base |

### FreeRTOS（CMSIS-V2）
- `defaultTask`：priority Normal，stack 512 words；於 `StartDefaultTask` 中輪詢 `DmaRecHalBuffCplt` / `DmaRecBuffCplt` 旗標並把樣本經 `>>8` 後印出
- Binary semaphore `adcSem`（dynamic）

### 其他
- DMA1（Channel1 給 ADC1、Channel4 給 DFSDM1_FLT0）
- USART1 @ 115200 8N1，重導 `__io_putchar` → `printf` 輸出至 ST-LINK VCP
- NVIC priority group 4

---

## 系統時脈

- 來源：MSI → PLL，**SYSCLK = 80 MHz**
- DFSDM clock = 80 MHz；ADC clock = 48 MHz（PLLSAI1）

---

## 專案結構

```
Lab6/
├── Lab6.ioc                       # CubeMX 設定（修改設定請從這裡）
├── CMakeLists.txt / CMakePresets.json
├── cmake/gcc-arm-none-eabi.cmake  # ARM GCC toolchain file
├── STM32L475XX_FLASH.ld           # linker script
├── startup_stm32l475xx.s
├── Core/
│   ├── Inc/                       # main.h, FreeRTOSConfig.h, stm32l4xx_hal_conf.h, *_it.h
│   └── Src/
│       ├── main.c                 # 周邊初始化 + DFSDM 回呼 + StartDefaultTask
│       ├── freertos.c             # FreeRTOS 應用區塊
│       ├── stm32l4xx_it.c         # 中斷處理
│       ├── stm32l4xx_hal_msp.c    # MSP（含 DMA request 對應）
│       └── stm32l4xx_hal_timebase_tim.c
├── Drivers/                       # STM32 HAL / CMSIS（自動產生，勿改）
└── Middlewares/                   # FreeRTOS kernel（自動產生，勿改）
```

### 與本 issue 相關的關鍵程式碼
- `Core/Src/main.c`
  - `MX_DFSDM1_Init()` — DFSDM filter / channel 設定
  - `main()` 中 `HAL_DFSDM_FilterRegularStart_DMA(&hdfsdm1_filter0, RecBuf, AUDIO_REC)`（`USER CODE BEGIN 2`）
  - `HAL_DFSDM_FilterRegConvHalfCpltCallback / RegConvCpltCallback / ErrorCallback`（`USER CODE BEGIN 4`）
  - `StartDefaultTask()` — 緩衝區輪詢與輸出
- `Core/Src/stm32l4xx_hal_msp.c` — DFSDM 的 DMA request 對應（已確認 `DMA_REQUEST_0` 正確，見「Issue 進展與更正」）

---

## 建置與燒錄

本專案使用 **CMake + Ninja + arm-none-eabi-gcc**（非 STM32CubeIDE Makefile 流程）。

需求：CMake ≥ 3.22、Ninja、Arm GNU Toolchain（`arm-none-eabi-gcc`）。

```bash
# 設定（Debug）
cmake --preset Debug

# 建置
cmake --build --preset Debug

# 產物位於 build/Debug/Lab6.elf
```

亦可直接用 STM32CubeIDE 匯入本資料夾，或以 STM32CubeProgrammer / ST-LINK 燒錄 `build/Debug/Lab6.elf`。

### 觀察輸出
1. 燒錄後連接 ST-LINK 的虛擬序列埠（USART1）
2. 開啟序列監看器：**115200 8N1**
3. 正常情況下應持續看到 `Default Task`，並在 DMA 回呼觸發時印出 `RegConvHalfCpltCallback` / `RegConvCpltCallback` 與 `PlayBuf` 樣本
4. 目前的 bug：上述回呼不會出現（DMA stall）

---

## 開發規則（重要）

本專案大量檔案由 **STM32CubeMX** 從 `Lab6.ioc` 自動產生。修改前務必遵守 [`.claude/rules/file-modification-scope`](./.claude/rules/file-modification-scope) 與 [`CLAUDE.md`](./CLAUDE.md)：

- ✅ **可自由修改**：`Core/**`（僅限 `/* USER CODE BEGIN ... END */` 區塊內）、`README.md`
- ⚠️ **設定層改動**（FreeRTOS、Pinout/GPIO、Clock、周邊參數、NVIC…）→ **不可手改原始碼**，需先告知 → 由維護者改 `.ioc` 重新 generate
- 🚫 **勿動**：`Drivers/`、`Middlewares/`、linker script、startup、`.ioc`、CMake 等自動產生 / 設定檔案，需修改請先詢問

> 簡言之：要改設定就改 `.ioc` 重生程式碼；要寫應用邏輯就待在 `Core/` 的 USER CODE 區塊內，並維持 surgical changes 原則。

---

## 參考

- GitHub Issue：[#1](https://github.com/Embedded-in-your-heart/Intelligent-home-STM32-client/issues/1)
- STM32L475 RM0351 Rev 9, **Table 44**（DMA1 requests for each channel）— `DMA1_Channel4` `C4S=0` = DFSDM1_FLT0
- 主專案：`Intelligent-home-STM32-client`
