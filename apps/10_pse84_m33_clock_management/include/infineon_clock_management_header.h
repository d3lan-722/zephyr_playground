#ifndef CLOCK_IFX_DATA_H
#define CLOCK_IFX_DATA_H

#include <zephyr/devicetree.h>
#include <stdint.h>

typedef struct{
    bool pll_enable;
    uint8_t bypass_sel;
    uint32_t fb_div;
    uint32_t ref_div;
    uint32_t output_div;
    bool dco_mode;
    uint32_t fraction_div;
}ifx_dpll_lp_spec;

typedef struct{
    uint8_t selector;
    uint32_t clock_division;
}ifx_hf_clock_spec;

/*
ROOT
*/
#define Z_CLOCK_MANAGEMENT_DATA_DEFINE_infineon_clock_root(node_id, prop, idx) 	\
	static const uint32_t Z_CLOCK_DATA_##node_id##_##idx =                      \
    DT_PHA_BY_IDX(node_id, prop, idx, root_freq);                               \
/*Pointer to the specifier value. This pointer is passed to the APIS.*/
#define Z_CLOCK_MANAGEMENT_DATA_GET_infineon_clock_root(node_id, prop, idx) 	\
	((void *)&Z_CLOCK_DATA_##node_id##_##idx)

/*Macro that stores the specifier value for being used by the framework.
MUX
*/
#define Z_CLOCK_MANAGEMENT_DATA_DEFINE_infineon_clock_mux(node_id, prop, idx) 	\
	static const uint32_t Z_CLOCK_DATA_##node_id##_##idx = 			\
	DT_PHA_BY_IDX(node_id, prop, idx, mux);
/*Pointer to the specifier value. This pointer is passed to the APIS.*/
#define Z_CLOCK_MANAGEMENT_DATA_GET_infineon_clock_mux(node_id, prop, idx) 	\
	((void *)&Z_CLOCK_DATA_##node_id##_##idx)
/*
DIVIDER
*/
#define Z_CLOCK_MANAGEMENT_DATA_DEFINE_infineon_clock_div(node_id, prop, idx) 	\
	static const uint32_t Z_CLOCK_DATA_##node_id##_##idx = 			\
	DT_PHA_BY_IDX(node_id, prop, idx, div);
#define Z_CLOCK_MANAGEMENT_DATA_GET_infineon_clock_div(node_id, prop, idx) 	\
	((void *)&Z_CLOCK_DATA_##node_id##_##idx)
/*
PLL
*/
#define Z_CLOCK_MANAGEMENT_DATA_DEFINE_infineon_clock_pll(node_id, prop, idx) 	\
	static const ifx_dpll_lp_spec Z_CLOCK_DATA_##node_id##_##idx = {            \
        .pll_enable = DT_PHA_BY_IDX(node_id, prop, idx, enable),                \
        .bypass_sel = DT_PHA_BY_IDX(node_id, prop, idx, bypass),          	    \
        .fb_div     = DT_PHA_BY_IDX(node_id, prop, idx, feedback),        	    \
        .ref_div    = DT_PHA_BY_IDX(node_id, prop, idx, reference),       	    \
        .output_div = DT_PHA_BY_IDX(node_id, prop, idx, output),          	    \
        .dco_mode   = (bool)DT_PHA_BY_IDX(node_id, prop, idx, dco),            	    \
        .fraction_div = DT_PHA_BY_IDX(node_id, prop, idx, fraction),     	    \
    	};
#define Z_CLOCK_MANAGEMENT_DATA_GET_infineon_clock_pll(node_id, prop, idx) 	    \
	((void *)&Z_CLOCK_DATA_##node_id##_##idx)
/*
HIGH FREQU
*/
#define Z_CLOCK_MANAGEMENT_DATA_DEFINE_infineon_clock_hf(node_id, prop, idx) 	\
	static const ifx_hf_clock_spec Z_CLOCK_DATA_##node_id##_##idx = {           \
        .selector = DT_PHA_BY_IDX(node_id, prop, idx, path_sel),                \
        .clock_division = DT_PHA_BY_IDX(node_id, prop, idx, div_value),         \
    };
/*Pointer to the specifier value. This pointer is passed to the APIS.*/
#define Z_CLOCK_MANAGEMENT_DATA_GET_infineon_clock_hf(node_id, prop, idx) 	    \
	((void *)&Z_CLOCK_DATA_##node_id##_##idx)

#endif