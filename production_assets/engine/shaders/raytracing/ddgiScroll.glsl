// std140 image of DdgiScrollGpu. Binding is selected by the consuming pass.
layout(std140,set=0,binding=DDGI_SCROLL_BINDING) uniform DdgiScrollBlock {
    vec4 OriginSpacing;
    ivec4 Delta;
    ivec4 Counts;
    ivec4 Rows[256];
} DdgiScroll;
