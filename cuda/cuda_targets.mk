COMMA := ,
GENCODE = -gencode arch=compute_$(1)$(COMMA)code=sm_$(1)
GENCODE_PTX = -gencode arch=compute_$(1)$(COMMA)code=compute_$(1)

NVCC_AVAILABLE_CODES := $(shell $(NVCC) --list-gpu-code 2>/dev/null)
HAS_SM_61 := $(filter sm_61,$(NVCC_AVAILABLE_CODES))
HAS_SM_75 := $(filter sm_75,$(NVCC_AVAILABLE_CODES))
HAS_SM_86 := $(filter sm_86,$(NVCC_AVAILABLE_CODES))
HAS_SM_89 := $(filter sm_89,$(NVCC_AVAILABLE_CODES))
HAS_SM_120 := $(filter sm_120,$(NVCC_AVAILABLE_CODES))
HAS_SM_121 := $(filter sm_121,$(NVCC_AVAILABLE_CODES))

GENCODE_PASCAL = $(call GENCODE,61)
GENCODE_TURING = $(call GENCODE,75)
GENCODE_AMPERE = $(call GENCODE,86)
GENCODE_ADA = $(call GENCODE,89)
GENCODE_BLACKWELL = $(strip $(if $(HAS_SM_120),$(call GENCODE,120),) $(if $(HAS_SM_121),$(call GENCODE,121),))
GENCODE_MODERN = $(strip $(if $(HAS_SM_89),$(call GENCODE,89),) $(if $(HAS_SM_120),$(call GENCODE,120),) $(if $(HAS_SM_121),$(call GENCODE,121),))
PTX_FALLBACK = $(strip \
	$(if $(HAS_SM_120),$(call GENCODE_PTX,120),\
	  $(if $(HAS_SM_89),$(call GENCODE_PTX,89),\
	    $(if $(HAS_SM_86),$(call GENCODE_PTX,86),\
	      $(if $(HAS_SM_75),$(call GENCODE_PTX,75),\
	        $(if $(HAS_SM_61),$(call GENCODE_PTX,61),))))))

$(CUDA_OBJ_DIR)/bdssim_sm61.o: bdssim.cu
	@mkdir -p $(CUDA_OBJ_DIR)
	@if ! $(NVCC) --list-gpu-code 2>/dev/null | grep -qw sm_61; then echo "[make] sm_61 unsupported by current NVCC ($(NVCC))."; exit 2; fi
	$(NVCC) $(NVCCFLAGS_BASE) $(GENCODE_PASCAL) -c $< -o $@

$(CUDA_OBJ_DIR)/bdssim_sm75.o: bdssim.cu
	@mkdir -p $(CUDA_OBJ_DIR)
	@if ! $(NVCC) --list-gpu-code 2>/dev/null | grep -qw sm_75; then echo "[make] sm_75 unsupported by current NVCC ($(NVCC))."; exit 2; fi
	$(NVCC) $(NVCCFLAGS_BASE) $(GENCODE_TURING) -c $< -o $@

$(CUDA_OBJ_DIR)/bdssim_sm86.o: bdssim.cu
	@mkdir -p $(CUDA_OBJ_DIR)
	@if ! $(NVCC) --list-gpu-code 2>/dev/null | grep -qw sm_86; then echo "[make] sm_86 unsupported by current NVCC ($(NVCC))."; exit 2; fi
	$(NVCC) $(NVCCFLAGS_BASE) $(GENCODE_AMPERE) -c $< -o $@

$(CUDA_OBJ_DIR)/bdssim_sm89.o: bdssim.cu
	@mkdir -p $(CUDA_OBJ_DIR)
	@if ! $(NVCC) --list-gpu-code 2>/dev/null | grep -qw sm_89; then echo "[make] sm_89 unsupported by current NVCC ($(NVCC))."; exit 2; fi
	$(NVCC) $(NVCCFLAGS_BASE) $(GENCODE_ADA) -c $< -o $@

$(CUDA_OBJ_DIR)/bdssim_sm120plus.o: bdssim.cu
	@mkdir -p $(CUDA_OBJ_DIR)
	@if [ -z "$(GENCODE_BLACKWELL)" ]; then echo "[make] sm_120/sm_121 unsupported by current NVCC ($(NVCC))."; exit 2; fi
	$(NVCC) $(NVCCFLAGS_BASE) $(GENCODE_BLACKWELL) -c $< -o $@

$(CUDA_OBJ_DIR)/bdssim_modern.o: bdssim.cu
	@mkdir -p $(CUDA_OBJ_DIR)
	@if [ -z "$(GENCODE_MODERN)" ]; then echo "[make] no modern gencode available (need sm_89 and/or sm_120/sm_121)."; exit 2; fi
	$(NVCC) $(NVCCFLAGS_BASE) $(GENCODE_MODERN) -c $< -o $@
