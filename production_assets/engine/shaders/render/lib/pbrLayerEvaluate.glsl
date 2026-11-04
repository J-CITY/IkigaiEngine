#if IKIGAI_PBR_LAYER_COUNT > 0
#ifdef IKIGAI_PBR_COVERAGE_ONLY
    pbrApplyCoverage(surface, inputUV, pbr_Layers.Layer0Color, pbr_Layers.Layer0UV, pbr_Layers.Layer0MaskUV, pbr_Layers.Layer0Blend, pbr_Layers.Layer0Height, pbr_Layer0AlbedoMap, pbr_Layer0MaskMap, pbr_Layer0HeightMap);
#else
    pbrApplySurface(surface, inputUV, pbr_Layers.Layer0Color, pbr_Layers.Layer0Factors, pbr_Layers.Layer0UV, pbr_Layers.Layer0MaskUV, pbr_Layers.Layer0Blend, pbr_Layers.Layer0Height, pbr_Layer0AlbedoMap, pbr_Layer0NormalMap, pbr_Layer0OrmMap, pbr_Layer0MaskMap, pbr_Layer0HeightMap);
#endif
#endif
#if IKIGAI_PBR_LAYER_COUNT > 1
#ifdef IKIGAI_PBR_COVERAGE_ONLY
    pbrApplyCoverage(surface, inputUV, pbr_Layers.Layer1Color, pbr_Layers.Layer1UV, pbr_Layers.Layer1MaskUV, pbr_Layers.Layer1Blend, pbr_Layers.Layer1Height, pbr_Layer1AlbedoMap, pbr_Layer1MaskMap, pbr_Layer1HeightMap);
#else
    pbrApplySurface(surface, inputUV, pbr_Layers.Layer1Color, pbr_Layers.Layer1Factors, pbr_Layers.Layer1UV, pbr_Layers.Layer1MaskUV, pbr_Layers.Layer1Blend, pbr_Layers.Layer1Height, pbr_Layer1AlbedoMap, pbr_Layer1NormalMap, pbr_Layer1OrmMap, pbr_Layer1MaskMap, pbr_Layer1HeightMap);
#endif
#endif
#if IKIGAI_PBR_LAYER_COUNT > 2
#ifdef IKIGAI_PBR_COVERAGE_ONLY
    pbrApplyCoverage(surface, inputUV, pbr_Layers.Layer2Color, pbr_Layers.Layer2UV, pbr_Layers.Layer2MaskUV, pbr_Layers.Layer2Blend, pbr_Layers.Layer2Height, pbr_Layer2AlbedoMap, pbr_Layer2MaskMap, pbr_Layer2HeightMap);
#else
    pbrApplySurface(surface, inputUV, pbr_Layers.Layer2Color, pbr_Layers.Layer2Factors, pbr_Layers.Layer2UV, pbr_Layers.Layer2MaskUV, pbr_Layers.Layer2Blend, pbr_Layers.Layer2Height, pbr_Layer2AlbedoMap, pbr_Layer2NormalMap, pbr_Layer2OrmMap, pbr_Layer2MaskMap, pbr_Layer2HeightMap);
#endif
#endif
