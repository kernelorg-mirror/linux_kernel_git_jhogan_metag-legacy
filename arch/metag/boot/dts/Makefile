dtb-y	+= skeleton.dtb
dtb-y	+= chorus2_generic.dtb
dtb-y	+= tz1090_generic.dtb
dtb-y	+= tz1090_01ry.dtb
dtb-y	+= tz1090_01sp.dtb
dtb-y	+= tz1090_01tt.dtb
dtb-y	+= tz1090_01xk.dtb

# Built-in dtb
builtindtb-y				:= skeleton
builtindtb-$(CONFIG_SOC_CHORUS2)	:= chorus2_generic
builtindtb-$(CONFIG_SOC_TZ1090)		:= tz1090_generic
builtindtb-$(CONFIG_COMET_BUB)		:= tz1090_01ry
builtindtb-$(CONFIG_TZ1090_01XX)	:= tz1090_01sp
builtindtb-$(CONFIG_POLARIS)		:= tz1090_01xk

ifneq ($(CONFIG_METAG_BUILTIN_DTB_NAME),"")
	builtindtb-y			:= $(patsubst "%",%,$(CONFIG_METAG_BUILTIN_DTB_NAME))
endif

dtb-$(CONFIG_METAG_BUILTIN_DTB)	+= $(builtindtb-y).dtb
obj-$(CONFIG_METAG_BUILTIN_DTB)	+= $(builtindtb-y).dtb.o

targets	+= dtbs
targets	+= $(dtb-y)

.SECONDARY: $(obj)/$(builtindtb-y).dtb.S

dtbs: $(addprefix $(obj)/, $(dtb-y))

clean-files += *.dtb *.dtb.S
