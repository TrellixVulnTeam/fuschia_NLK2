// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_REGISTERS_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_REGISTERS_H_

#include <lib/mmio/mmio.h>

#include <ddk/mmio-buffer.h>
#include <hwreg/bitfields.h>

template <class RegType>
class TypedRegisterAddr;

// Typed registers can be used only with a specific type of RegisterIo.
template <class MmioType, class DerivedType, class IntType, class PrinterState = void>
class TypedRegisterBase : public hwreg::RegisterBase<DerivedType, IntType, PrinterState> {
 public:
  using SelfType = DerivedType;
  using ValueType = IntType;
  using Mmio = MmioType;
  using AddrType = TypedRegisterAddr<SelfType>;
  SelfType& ReadFrom(MmioType* reg_io) {
    return hwreg::RegisterBase<DerivedType, IntType, PrinterState>::ReadFrom(
        static_cast<ddk::MmioBuffer*>(reg_io));
  }
  SelfType& WriteTo(MmioType* reg_io) {
    return hwreg::RegisterBase<DerivedType, IntType, PrinterState>::WriteTo(
        static_cast<ddk::MmioBuffer*>(reg_io));
  }
};

template <class RegType>
class TypedRegisterAddr : public hwreg::RegisterAddr<RegType> {
 public:
  TypedRegisterAddr(uint32_t reg_addr) : hwreg::RegisterAddr<RegType>(reg_addr) {}

  RegType ReadFrom(typename RegType::Mmio* reg_io) {
    RegType reg;
    reg.set_reg_addr(hwreg::RegisterAddr<RegType>::addr());
    reg.ReadFrom(reg_io);
    return reg;
  }
};

// Cbus does a lot of things, but mainly seems to handle audio and video
// processing.
class CbusRegisterIo : public ddk::MmioBuffer {
 public:
  CbusRegisterIo(const mmio_buffer_t& mmio) : ddk::MmioBuffer(mmio) {}
};

// The DOS bus mainly seems to handle video decoding.
class DosRegisterIo : public ddk::MmioBuffer {
 public:
  DosRegisterIo(const mmio_buffer_t& mmio) : ddk::MmioBuffer(mmio) {}
};

// Aobus communicates with the always-on power management processor.
class AoRegisterIo : public ddk::MmioBuffer {
 public:
  AoRegisterIo(const mmio_buffer_t& mmio) : ddk::MmioBuffer(mmio) {}
};

// Hiubus mainly seems to handle clock control and gating.
class HiuRegisterIo : public ddk::MmioBuffer {
 public:
  HiuRegisterIo(const mmio_buffer_t& mmio) : ddk::MmioBuffer(mmio) {}
};

// The DMC is the DDR memory controller.
class DmcRegisterIo : public ddk::MmioBuffer {
 public:
  DmcRegisterIo(const mmio_buffer_t& mmio) : ddk::MmioBuffer(mmio) {}
};

class ResetRegisterIo : public ddk::MmioView {
 public:
  ResetRegisterIo(const mmio_buffer_t& mmio, zx_off_t off) : ddk::MmioView(mmio, off) {}
};

class ParserRegisterIo : public ddk::MmioView {
 public:
  ParserRegisterIo(const mmio_buffer_t& mmio, zx_off_t off) : ddk::MmioView(mmio, off) {}
};

class DemuxRegisterIo : public ddk::MmioView {
 public:
  DemuxRegisterIo(const mmio_buffer_t& mmio, zx_off_t off) : ddk::MmioView(mmio, off) {}
};

#define DEFINE_REGISTER(name, type, address)                           \
  class name : public TypedRegisterBase<type, name, uint32_t> {        \
   public:                                                             \
    static auto Get() { return TypedRegisterAddr<name>((address)*4); } \
  };

#define REGISTER_NAME(name, type, address)                      \
  class name : public TypedRegisterBase<type, name, uint32_t> { \
   public:                                                      \
    static auto Get() { return AddrType((address)*4); }

// clang-format off
DEFINE_REGISTER(Mpsr, DosRegisterIo, 0x301);
DEFINE_REGISTER(Cpsr, DosRegisterIo, 0x321);
DEFINE_REGISTER(ImemDmaCtrl, DosRegisterIo, 0x340);
DEFINE_REGISTER(ImemDmaAdr, DosRegisterIo, 0x341);
DEFINE_REGISTER(ImemDmaCount, DosRegisterIo, 0x342);
DEFINE_REGISTER(LmemDmaCtrl, DosRegisterIo, 0x0350);
DEFINE_REGISTER(DcacDmaCtrl, DosRegisterIo, 0x0e12);
DEFINE_REGISTER(DosSwReset0, DosRegisterIo, 0x3f00);
DEFINE_REGISTER(DosGclkEn, DosRegisterIo, 0x3f01);
DEFINE_REGISTER(DosMemPdVdec, DosRegisterIo, 0x3f30);
DEFINE_REGISTER(DosMemPdHevc, DosRegisterIo, 0x3f33);

REGISTER_NAME(DosSwReset3, DosRegisterIo, 0x3f34)
  DEF_BIT(11, mcpu);
  DEF_BIT(12, ccpu);
};

DEFINE_REGISTER(DosGclkEn3, DosRegisterIo, 0x3f35);
DEFINE_REGISTER(DosVdecMcrccStallCtrl, DosRegisterIo, 0x3f40);

DEFINE_REGISTER(VldMemVififoStartPtr, DosRegisterIo, 0x0c40)
DEFINE_REGISTER(VldMemVififoCurrPtr, DosRegisterIo, 0x0c41)
DEFINE_REGISTER(VldMemVififoEndPtr, DosRegisterIo, 0x0c42)
DEFINE_REGISTER(VldMemVififoBytesAvail, DosRegisterIo, 0x0c43)

REGISTER_NAME(VldMemVififoControl, DosRegisterIo, 0x0c44)
  DEF_FIELD(23, 16, upper);
  DEF_BIT(10, fill_on_level);
  DEF_FIELD(6, 3, endianness);
  DEF_BIT(2, empty_en);
  DEF_BIT(1, fill_en);
  DEF_BIT(0, init);
};

DEFINE_REGISTER(VldMemVififoWP, DosRegisterIo, 0x0c45)
DEFINE_REGISTER(VldMemVififoRP, DosRegisterIo, 0x0c46)
DEFINE_REGISTER(VldMemVififoLevel, DosRegisterIo, 0x0c47)
REGISTER_NAME(VldMemVififoBufCntl, DosRegisterIo, 0x0c48)
    DEF_BIT(1, manual);
    DEF_BIT(0, init);
};
DEFINE_REGISTER(VldMemVififoWrapCount, DosRegisterIo, 0x0c51)
DEFINE_REGISTER(VldMemVififoMemCtl, DosRegisterIo, 0x0c52)

DEFINE_REGISTER(PowerCtlVld, DosRegisterIo, 0x0c08)
REGISTER_NAME(DosGenCtrl0, DosRegisterIo, 0x3f02)
  enum {
    kVdec = 0,
    kHevc = 3
  };

  // Which core's input read pointer is plumbed into the parser's RP.
  DEF_FIELD(2, 1, vbuf_rp_select);
};

DEFINE_REGISTER(McStatus0, DosRegisterIo, 0x0909)
DEFINE_REGISTER(McCtrl1, DosRegisterIo, 0x090b)
DEFINE_REGISTER(DblkCtrl, DosRegisterIo, 0x0951)
DEFINE_REGISTER(DblkStatus, DosRegisterIo, 0x0953)
REGISTER_NAME(MdecPicDcCtrl, DosRegisterIo, 0x098e)
    DEF_BIT(17, nv12_output); // as opposed to 3-plane YUV
    DEF_BIT(31, bit31);
};
DEFINE_REGISTER(MdecPicDcStatus, DosRegisterIo, 0x098f)

DEFINE_REGISTER(MdecSwReset, DosRegisterIo, 0x0984)
DEFINE_REGISTER(MdecPicDcThresh, DosRegisterIo, 0x9b8)

// AvScratch registers are used to communicate with the AMRISC coprocessor.
class AvScratch : public TypedRegisterBase<DosRegisterIo, AvScratch, uint32_t> {
 public:
    static auto Get(uint32_t i) { return AddrType((0x09c0 + i) * 4); }
};

DEFINE_REGISTER(AvScratch0, DosRegisterIo, 0x09c0)
DEFINE_REGISTER(AvScratch1, DosRegisterIo, 0x09c1)
DEFINE_REGISTER(AvScratch2, DosRegisterIo, 0x09c2)
DEFINE_REGISTER(AvScratch3, DosRegisterIo, 0x09c3)
DEFINE_REGISTER(AvScratch4, DosRegisterIo, 0x09c4)
DEFINE_REGISTER(AvScratch5, DosRegisterIo, 0x09c5)
DEFINE_REGISTER(AvScratch6, DosRegisterIo, 0x09c6)
DEFINE_REGISTER(AvScratch7, DosRegisterIo, 0x09c7)
DEFINE_REGISTER(AvScratch8, DosRegisterIo, 0x09c8)
DEFINE_REGISTER(AvScratch9, DosRegisterIo, 0x09c9)
DEFINE_REGISTER(AvScratchA, DosRegisterIo, 0x09ca)
DEFINE_REGISTER(AvScratchB, DosRegisterIo, 0x09cb)
DEFINE_REGISTER(AvScratchC, DosRegisterIo, 0x09cc)
DEFINE_REGISTER(AvScratchD, DosRegisterIo, 0x09cd)
DEFINE_REGISTER(AvScratchE, DosRegisterIo, 0x09ce)
DEFINE_REGISTER(AvScratchF, DosRegisterIo, 0x09cf)
DEFINE_REGISTER(AvScratchG, DosRegisterIo, 0x09d0)
DEFINE_REGISTER(AvScratchH, DosRegisterIo, 0x09d1)
DEFINE_REGISTER(AvScratchI, DosRegisterIo, 0x09d2)
DEFINE_REGISTER(AvScratchJ, DosRegisterIo, 0x09d3)
DEFINE_REGISTER(AvScratchK, DosRegisterIo, 0x09d4)
DEFINE_REGISTER(AvScratchL, DosRegisterIo, 0x09d5)
DEFINE_REGISTER(AvScratchM, DosRegisterIo, 0x09d6)
DEFINE_REGISTER(AvScratchN, DosRegisterIo, 0x09d7)

DEFINE_REGISTER(Mpeg12Reg, DosRegisterIo, 0x0c01)
DEFINE_REGISTER(PscaleCtrl, DosRegisterIo, 0x0911)
DEFINE_REGISTER(PicHeadInfo, DosRegisterIo, 0x0c03)
DEFINE_REGISTER(M4ControlReg, DosRegisterIo, 0x0c29)
DEFINE_REGISTER(VdecAssistMbox1ClrReg, DosRegisterIo, 0x0075)
DEFINE_REGISTER(VdecAssistMbox1Mask, DosRegisterIo, 0x0076)

class AncNCanvasAddr : public TypedRegisterBase<DosRegisterIo, AncNCanvasAddr, uint32_t> {
 public:
    static auto Get(uint32_t i) { return AddrType((0x0990 + i) * 4); }
};

DEFINE_REGISTER(HevcAssistMmuMapAddr, DosRegisterIo, 0x3009);
DEFINE_REGISTER(HevcAssistMbox0IrqReg, DosRegisterIo, 0x3070);
DEFINE_REGISTER(HevcAssistMbox0ClrReg, DosRegisterIo, 0x3071);
DEFINE_REGISTER(HevcAssistMbox0Mask, DosRegisterIo, 0x3072);

DEFINE_REGISTER(HevcAssistScratch0, DosRegisterIo, 0x30c0);
DEFINE_REGISTER(HevcAssistScratch1, DosRegisterIo, 0x30c1);
DEFINE_REGISTER(HevcAssistScratch2, DosRegisterIo, 0x30c2);
DEFINE_REGISTER(HevcAssistScratch3, DosRegisterIo, 0x30c3);
DEFINE_REGISTER(HevcAssistScratch4, DosRegisterIo, 0x30c4);
DEFINE_REGISTER(HevcAssistScratch5, DosRegisterIo, 0x30c5);
DEFINE_REGISTER(HevcAssistScratch6, DosRegisterIo, 0x30c6);
DEFINE_REGISTER(HevcAssistScratch7, DosRegisterIo, 0x30c7);
DEFINE_REGISTER(HevcAssistScratch8, DosRegisterIo, 0x30c8);
DEFINE_REGISTER(HevcAssistScratch9, DosRegisterIo, 0x30c9);
DEFINE_REGISTER(HevcAssistScratchA, DosRegisterIo, 0x30ca);
DEFINE_REGISTER(HevcAssistScratchB, DosRegisterIo, 0x30cb);
DEFINE_REGISTER(HevcAssistScratchC, DosRegisterIo, 0x30cc);
DEFINE_REGISTER(HevcAssistScratchD, DosRegisterIo, 0x30cd);
DEFINE_REGISTER(HevcAssistScratchE, DosRegisterIo, 0x30ce);
DEFINE_REGISTER(HevcAssistScratchF, DosRegisterIo, 0x30cf);
DEFINE_REGISTER(HevcAssistScratchG, DosRegisterIo, 0x30d0);
DEFINE_REGISTER(HevcAssistScratchH, DosRegisterIo, 0x30d1);
DEFINE_REGISTER(HevcAssistScratchI, DosRegisterIo, 0x30d2);
DEFINE_REGISTER(HevcAssistScratchJ, DosRegisterIo, 0x30d3);
DEFINE_REGISTER(HevcAssistScratchK, DosRegisterIo, 0x30d4);
DEFINE_REGISTER(HevcAssistScratchL, DosRegisterIo, 0x30d5);
DEFINE_REGISTER(HevcAssistScratchM, DosRegisterIo, 0x30d6);
DEFINE_REGISTER(HevcAssistScratchN, DosRegisterIo, 0x30d7);

REGISTER_NAME(HevcStreamControl, DosRegisterIo, 0x3101)
  enum {
    kBigEndian64 = 0,
    kLittleEndian64 = 7,
  };
  DEF_BIT(0, stream_fetch_enable);
  DEF_BIT(3, use_parser_vbuf_wp); // Use parser video wp instead of StreamWrPtr
  DEF_FIELD(7, 4 , endianness);
  DEF_BIT(15, force_power_on);
};
DEFINE_REGISTER(HevcStreamStartAddr, DosRegisterIo, 0x3102);
DEFINE_REGISTER(HevcStreamEndAddr, DosRegisterIo, 0x3103);
DEFINE_REGISTER(HevcStreamWrPtr, DosRegisterIo, 0x3104);
DEFINE_REGISTER(HevcStreamRdPtr, DosRegisterIo, 0x3105);
REGISTER_NAME(HevcStreamFifoCtl, DosRegisterIo, 0x3107)
  DEF_BIT(29, stream_fifo_hole);
};
REGISTER_NAME(HevcShiftControl, DosRegisterIo, 0x3108)
  DEF_BIT(14, start_code_protect);
  DEF_BIT(10, length_zero_startcode);
  DEF_BIT(9, length_valid_startcode);
  DEF_FIELD(7, 6, sft_valid_wr_position);
  DEF_FIELD(5, 4, emulate_code_length_minus1);
  DEF_FIELD(2, 1, start_code_length_minus1);
  DEF_BIT(0, stream_shift_enable);
};
DEFINE_REGISTER(HevcShiftStartCode, DosRegisterIo, 0x3109);
DEFINE_REGISTER(HevcShiftEmulateCode, DosRegisterIo, 0x310a);
REGISTER_NAME(HevcShiftStatus, DosRegisterIo, 0x310b)
  DEF_BIT(1, emulation_check);
  DEF_BIT(0, startcode_check);
};
DEFINE_REGISTER(HevcShiftByteCount, DosRegisterIo, 0x310d);

REGISTER_NAME(HevcParserIntControl, DosRegisterIo, 0x3120)
  DEF_FIELD(31, 29, fifo_ctl);
  DEF_BIT(24, stream_buffer_empty_amrisc_enable);
  DEF_BIT(22, stream_fifo_empty_amrisc_enable);
  DEF_BIT(7, dec_done_int_cpu_enable);
  DEF_BIT(4, startcode_found_int_cpu_enable);
  DEF_BIT(3, startcode_found_int_amrisc_enable);
  DEF_BIT(0, parser_int_enable);
};
DEFINE_REGISTER(HevcParserIntStatus, DosRegisterIo, 0x3121);
DEFINE_REGISTER(HevcParserPictureSize, DosRegisterIo, 0x3123);
DEFINE_REGISTER(HevcParserLcuStart, DosRegisterIo, 0x3124);

DEFINE_REGISTER(HevcStreamLevel, DosRegisterIo, 0x3106);
REGISTER_NAME(HevcCabacControl, DosRegisterIo, 0x3110)
  DEF_BIT(0, enable);
};

REGISTER_NAME(HevcParserCoreControl, DosRegisterIo, 0x3113)
  DEF_BIT(0, clock_enable);
};
DEFINE_REGISTER(HevcStreamSwapAddr, DosRegisterIo, 0x3134);
REGISTER_NAME(HevcStreamSwapCtrl, DosRegisterIo, 0x3135)
  DEF_BIT(0, enable);
  DEF_BIT(1, save); // 0 means restore
  DEF_BIT(7, in_progress);
};
DEFINE_REGISTER(HevcIqitScalelutWrAddr, DosRegisterIo, 0x3702);
DEFINE_REGISTER(HevcIqitScalelutData, DosRegisterIo, 0x3704);
DEFINE_REGISTER(HevcParserCmdWrite, DosRegisterIo, 0x3112);
REGISTER_NAME(HevcParserIfControl, DosRegisterIo, 0x3122)
  DEF_BIT(8, sao_sw_pred_enable);
  DEF_BIT(5, parser_sao_if_enable);
  DEF_BIT(2, parser_mpred_if_enable);
  DEF_BIT(0, parser_scaler_if_enable);
};

DEFINE_REGISTER(HevcParserCmdSkip0, DosRegisterIo, 0x3128);
DEFINE_REGISTER(HevcParserCmdSkip1, DosRegisterIo, 0x3129);
DEFINE_REGISTER(HevcParserCmdSkip2, DosRegisterIo, 0x312a);

DEFINE_REGISTER(HevcMpredAbvStartAddr, DosRegisterIo, 0x3212);
DEFINE_REGISTER(HevcMpredMvWrStartAddr, DosRegisterIo, 0x3213);
DEFINE_REGISTER(HevcMpredMvRdStartAddr, DosRegisterIo, 0x3214);
DEFINE_REGISTER(HevcMpredMvWptr, DosRegisterIo, 0x3215);
DEFINE_REGISTER(HevcMpredMvRptr, DosRegisterIo, 0x3216);
DEFINE_REGISTER(HevcMpredCurrLcu, DosRegisterIo, 0x3219);
DEFINE_REGISTER(HevcMpredCtrl3, DosRegisterIo, 0x321d);
REGISTER_NAME(HevcMpredCtrl4, DosRegisterIo, 0x324c)
  DEF_BIT(6, use_prev_frame_mvs);
};
DEFINE_REGISTER(HevcMpredMvRdEndAddr, DosRegisterIo, 0x3262);

DEFINE_REGISTER(HevcMpsr, DosRegisterIo, 0x3301);
DEFINE_REGISTER(HevcCpsr, DosRegisterIo, 0x3321);
DEFINE_REGISTER(HevcImemDmaCtrl, DosRegisterIo, 0x3340);
DEFINE_REGISTER(HevcImemDmaAdr, DosRegisterIo, 0x3341);
DEFINE_REGISTER(HevcImemDmaCount, DosRegisterIo, 0x3342);

REGISTER_NAME(HevcdIppTopCntl, DosRegisterIo, 0x3400)
  DEF_BIT(1, enable_ipp);
  DEF_BIT(0, reset_ipp_and_mpp);
};
DEFINE_REGISTER(HevcdIppLinebuffBase, DosRegisterIo, 0x3409);
REGISTER_NAME(HevcdIppAxiifConfig, DosRegisterIo, 0x340b)
  enum {
    kMemMapModeLinear = 0,
    kMemMapMode32x32 = 1,
    kMemMapMode64x32 = 2,
    kBigEndian64 = 0xf,
  };
  DEF_FIELD(5, 4, mem_map_mode);
  DEF_FIELD(3, 0, double_write_endian);
};

DEFINE_REGISTER(Vp9dMppRefScaleEnable, DosRegisterIo, 0x3441);
REGISTER_NAME(Vp9dMppRefinfoTblAccconfig, DosRegisterIo, 0x3442)
  DEF_BIT(2, bit2);
};
DEFINE_REGISTER(Vp9dMppRefinfoData, DosRegisterIo, 0x3443);
REGISTER_NAME(HevcdMppAnc2AxiTblConfAddr, DosRegisterIo, 0x3460)
  DEF_BIT(1, bit1);
  DEF_BIT(2, bit2);
};
DEFINE_REGISTER(HevcdMppAnc2AxiTblData, DosRegisterIo, 0x3464);
REGISTER_NAME(HevcdMppAncCanvasAccconfigAddr, DosRegisterIo, 0x34c0);
  DEF_BIT(0, bit0); // Probably signals a write.
  DEF_BIT(1, bit1);
  DEF_FIELD(15, 8, field15_8);
};
DEFINE_REGISTER(HevcdMppAncCanvasDataAddr, DosRegisterIo, 0x34c1);

REGISTER_NAME(HevcdMppDecompCtl1, DosRegisterIo, 0x34c2)
  DEF_BIT(31, use_uncompressed);
  DEF_BIT(4, paged_mode); // Allocate compressed pages on demand.
  DEF_BIT(3, smem_mode);
};

DEFINE_REGISTER(HevcdMppDecompCtl2, DosRegisterIo, 0x34c3);

REGISTER_NAME(HevcdMcrccCtl1, DosRegisterIo, 0x34f0)
  DEF_BIT(1, reset);
};
DEFINE_REGISTER(HevcdMcrccCtl2, DosRegisterIo, 0x34f1);
DEFINE_REGISTER(HevcdMcrccCtl3, DosRegisterIo, 0x34f2);

DEFINE_REGISTER(HevcDblkCfg4, DosRegisterIo, 0x3504);
DEFINE_REGISTER(HevcDblkCfg5, DosRegisterIo, 0x3505);
DEFINE_REGISTER(HevcDblkCfg9, DosRegisterIo, 0x3509);
DEFINE_REGISTER(HevcDblkCfgA, DosRegisterIo, 0x350a);
REGISTER_NAME(HevcDblkCfgB, DosRegisterIo, 0x350b)
  DEF_BIT(0, vp9_mode);
  DEF_FIELD(5, 4, pipeline_mode);
  DEF_BIT(8, compressed_write_enable);
  DEF_BIT(9, uncompressed_write_enable);
};
DEFINE_REGISTER(HevcDblkCfgE, DosRegisterIo, 0x350e)

REGISTER_NAME(HevcSaoCtrl1, DosRegisterIo, 0x3602)
  enum {
    kMemMapModeLinear = 0,
    kMemMapMode32x32 = 1,
    kMemMapMode64x32 = 2,

    kBigEndian64 = 0xff,
  };

  DEF_FIELD(13, 12, mem_map_mode);
  DEF_FIELD(11, 4, endianness);
  DEF_BIT(1, double_write_disable);
  DEF_BIT(0, compressed_write_disable);
};

DEFINE_REGISTER(HevcSaoYStartAddr, DosRegisterIo, 0x360b);
DEFINE_REGISTER(HevcSaoYLength, DosRegisterIo, 0x360c);
DEFINE_REGISTER(HevcSaoCStartAddr, DosRegisterIo, 0x360d);
DEFINE_REGISTER(HevcSaoCLength, DosRegisterIo, 0x360e);
DEFINE_REGISTER(HevcSaoYWptr, DosRegisterIo, 0x360f);
DEFINE_REGISTER(HevcSaoCWptr, DosRegisterIo, 0x3610);

REGISTER_NAME(HevcSaoCtrl5, DosRegisterIo, 0x3623)
  DEF_BIT(9, mode_8_bits);
  DEF_BIT(10, use_compressed_header);
};
DEFINE_REGISTER(HevcCmBodyStartAddr, DosRegisterIo, 0x3626);
DEFINE_REGISTER(HevcCmBodyLength, DosRegisterIo, 0x3627);
DEFINE_REGISTER(HevcCmHeaderStartAddr, DosRegisterIo, 0x3628);
DEFINE_REGISTER(HevcCmHeaderLength, DosRegisterIo, 0x3629);
DEFINE_REGISTER(HevcCmHeaderOffset, DosRegisterIo, 0x362b);

DEFINE_REGISTER(HevcSaoMmuVh0Addr, DosRegisterIo, 0x363a)
DEFINE_REGISTER(HevcSaoMmuVh1Addr, DosRegisterIo, 0x363b)

DEFINE_REGISTER(HevcPscaleCtrl, DosRegisterIo, 0x3911);
DEFINE_REGISTER(HevcDblkCtrl, DosRegisterIo, 0x3951);
DEFINE_REGISTER(HevcDblkStatus, DosRegisterIo, 0x3953);
DEFINE_REGISTER(HevcMdecPicDcCtrl, DosRegisterIo, 0x398e);
DEFINE_REGISTER(HevcMdecPicDcStatus, DosRegisterIo, 0x398f);
DEFINE_REGISTER(HevcDcacDmaCtrl, DosRegisterIo, 0x3e12);

DEFINE_REGISTER(AoRtiGenPwrSleep0, AoRegisterIo, 0x3a);
DEFINE_REGISTER(AoRtiGenPwrIso0, AoRegisterIo, 0x3b);

REGISTER_NAME(HhiGclkMpeg0, HiuRegisterIo, 0x50)
  DEF_BIT(1, dos);
};

REGISTER_NAME(HhiGclkMpeg1, HiuRegisterIo, 0x51)
  DEF_BIT(25, u_parser_top);
  DEF_FIELD(13, 6, aiu);
  DEF_BIT(4, demux);
  DEF_BIT(2, audio_in);
};

REGISTER_NAME(HhiGclkMpeg2, HiuRegisterIo, 0x52)
  DEF_BIT(25, vpu_interrupt);
};

REGISTER_NAME(HhiVdecClkCntl, HiuRegisterIo, 0x78)
  DEF_BIT(8, vdec_en);
  DEF_FIELD(11, 9, vdec_sel);
  DEF_FIELD(6, 0, vdec_div);
};

REGISTER_NAME(HhiHevcClkCntl, HiuRegisterIo, 0x79)
  DEF_BIT(24, vdec_en);
  DEF_FIELD(27, 25, vdec_sel);
  DEF_FIELD(22, 16, vdec_div);
  DEF_BIT(8, front_enable);
  DEF_FIELD(11, 9, front_sel);
  DEF_FIELD(6, 0, front_div);
};

REGISTER_NAME(DmcReqCtrl, DmcRegisterIo, 0x0)
  DEF_BIT(13, vdec);
};

DEFINE_REGISTER(Reset0Register, ResetRegisterIo, 0x1101 - 0x1100);
REGISTER_NAME(Reset1Register, ResetRegisterIo, 0x1102 - 0x1100)
  DEF_BIT(8, parser);
};
DEFINE_REGISTER(FecInputControl, DemuxRegisterIo, 0x1602)

REGISTER_NAME(TsHiuCtl, DemuxRegisterIo, 0x1625)
  DEF_BIT(7, use_hi_bsf_interface);
};
REGISTER_NAME(TsHiuCtl2, DemuxRegisterIo, 0x1675)
  DEF_BIT(7, use_hi_bsf_interface);
};
REGISTER_NAME(TsHiuCtl3, DemuxRegisterIo, 0x16c5)
  DEF_BIT(7, use_hi_bsf_interface);
};

REGISTER_NAME(TsFileConfig, DemuxRegisterIo, 0x16f2)
  DEF_BIT(5, ts_hiu_enable);
};

REGISTER_NAME(ParserConfig, ParserRegisterIo, 0x2965)
  enum {
    kWidth8 = 0,
    kWidth16 = 1,
    kWidth24 = 2,
    kWidth32 = 3,
  };
  DEF_FIELD(23, 16, pfifo_empty_cnt);
  DEF_FIELD(15, 12, max_es_write_cycle);
  DEF_FIELD(11, 10, startcode_width);
  DEF_FIELD(9, 8, pfifo_access_width);
  DEF_FIELD(7, 0, max_fetch_cycle);
};
DEFINE_REGISTER(PfifoWrPtr, ParserRegisterIo, 0x2966)
DEFINE_REGISTER(PfifoRdPtr, ParserRegisterIo, 0x2967)
DEFINE_REGISTER(ParserSearchPattern, ParserRegisterIo, 0x2969)
DEFINE_REGISTER(ParserSearchMask, ParserRegisterIo, 0x296a)

REGISTER_NAME(ParserControl, ParserRegisterIo, 0x2960)
  enum {
    kSearch = (1 << 1),
    kStart = (1 << 0),
    kAutoSearch = kSearch | kStart,
  };
  DEF_FIELD(31, 8, es_pack_size);
  DEF_FIELD(7, 6, type);
  DEF_BIT(5, write);
  DEF_FIELD(4, 0, command);
};

DEFINE_REGISTER(ParserVideoStartPtr, ParserRegisterIo, 0x2980)
DEFINE_REGISTER(ParserVideoEndPtr, ParserRegisterIo, 0x2981)
DEFINE_REGISTER(ParserVideoWp, ParserRegisterIo, 0x2982)
DEFINE_REGISTER(ParserVideoRp, ParserRegisterIo, 0x2983)

REGISTER_NAME(ParserEsControl, ParserRegisterIo, 0x2977)
  // Determines if the parser should swap around the endianness of the video output.
  DEF_FIELD(3, 1, video_write_endianness);
  DEF_BIT(0, video_manual_read_ptr_update);
};

REGISTER_NAME(ParserIntStatus, ParserRegisterIo, 0x296c)
  DEF_BIT(7, fetch_complete);
  DEF_BIT(0, start_code_found);
};
REGISTER_NAME(ParserIntEnable, ParserRegisterIo, 0x296b)
  DEF_BIT(8, host_en_start_code_found);
  DEF_BIT(15, host_en_fetch_complete);
};

DEFINE_REGISTER(ParserFetchAddr, ParserRegisterIo, 0x2961)
REGISTER_NAME(ParserFetchCmd, ParserRegisterIo, 0x2962)
  DEF_FIELD(29, 27, fetch_endian);
  DEF_FIELD(26, 0, len);
};

// clang-format on

#undef REGISTER_NAME
#undef DEFINE_REGISTER

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_REGISTERS_H_
