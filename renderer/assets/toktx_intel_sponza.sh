#/bin/bash

BASE_COLOR_TEXTURES=(
arch_stone_wall_01_BaseColor
brickwall_01_BaseColor
brickwall_02_BaseColor
ceiling_plaster_01_BaseColor
ceiling_plaster_02_BaseColor
col_1stfloor_BaseColor
col_brickwall_01_BaseColor
col_head_1stfloor_BaseColor
col_head_2ndfloor_02_BaseColor
col_head_2ndfloor_03_BaseColor
curtain_fabric_blue_BaseColor
curtain_fabric_green_BaseColor
curtain_fabric_red_BaseColor
door_stoneframe_01_BaseColor
door_stoneframe_02_BaseColor
floor_tiles_01_BaseColor
lionhead_01_BaseColor
metal_door_01_BaseColor
ornament_01_BaseColor
roof_tiles_01_BaseColor
stone_01_tile_BaseColor
stones_2ndfloor_01_BaseColor
stone_trims_01_BaseColor
stone_trims_02_BaseColor
window_frame_01_BaseColor
wood_door_01_BaseColor
wood_tile_01_BaseColor
)

NORMAL_TEXTURES=(
arch_stone_wall_01_Normal
brickwall_01_Normal
brickwall_02_Normal
ceiling_plaster_01_Normal
ceiling_plaster_02_Normal
col_1stfloor_Normal
col_brickwall_01_Normal
col_head_1stfloor_Normal
col_head_2ndfloor_02_Normal
col_head_2ndfloor_03_Normal
curtain_fabric_Normal
door_stoneframe_01_Normal
door_stoneframe_02_Normal
floor_tiles_01_Normal
lionhead_01_Normal
metal_door_01_Normal
ornament_01_Normal
roof_tiles_01_Normal
stone_01_tile_Normal
stones_2ndfloor_01_Normal
stone_trims_01_Normal
stone_trims_02_Normal
window_frame_01_Normal
wood_door_01_Normal
wood_tile_01_Normal
)

ROUGHNESS_METALNESS_TEXTURES=(
arch_stone_wall_01_Roughnessarch_stone_wall_01_Metalness
brickwall_01_Roughnessbrickwall_01_Metalness
brickwall_02_Roughnessbrickwall_02_Metalness
ceiling_plaster_01_Roughnessceiling_plaster_01_Metalness
ceiling_plaster_02_Roughnessceiling_plaster_01_Metalness
col_1stfloor_Roughnesscol_1stfloor_Metalness
col_brickwall_01_Roughnesscol_brickwall_01_Metalness
col_brickwall_01_Roughnesscolumn_brickwall_01_Metalness
col_head_1stfloor_Roughnesscol_head_1stfloor_Metalness
col_head_2ndfloor_02_Roughnesscol_head_2ndfloor_02_Metalness
col_head_2ndfloor_03_Roughnesscol_head_2ndfloor_03_Metalness
door_stoneframe_01_Roughnessdoor_stoneframe_01_Metalness
door_stoneframe_02_Roughnessdoor_stoneframe_02_Metalness
floor_tiles_01_Roughnessfloor_tiles_01_Metalness
lionhead_01_Roughnesslionhead_01_Metalness
metal_door_01_Roughnessmetal_door_01_Metalness
ornament_01_Roughnessornament_01_Metalness
roof_tiles_01_Roughnessroof_tiles_01_Metalness
stone_01_tile_Roughnessstone_01_tile_Metalness
stones_2ndfloor_01_Roughnessstones_2ndfloor_01_Metalness
stone_trims_01_Roughnessstone_trims_01_Metalness
stone_trims_02_Roughnessstone_trims_02_Metalness
window_frame_01_Roughnesswindow_frame_01_Metalness
wood_door_01_Roughnesswood_door_01_Metalness
wood_tile_01_Roughnesswood_tile_01_Metalness
)

DECAL_MASK_ALPHA_TEXTURES=(
dirt_decal_01_dirt_decal_01_mask_gltf_alpha_dirt_decal_Opacity
)

# https://evergine.com/ktx2-texture-compression/

DIR=./main_sponza/textures
cd "$DIR"

for tex in "${BASE_COLOR_TEXTURES[@]}";
do
    echo "creating ${tex}.ktx2"
    ktx create --encode uastc --generate-mipmap --assign-tf srgb --format R8G8B8_SRGB --uastc-quality 3 --uastc-rdo --uastc-rdo-l 4.0 "${tex}.png" "${tex}.ktx2"
    echo "transcoding ${tex}.ktx2"
    ktx transcode --target bc7 "${tex}.ktx2" "${tex}.ktx2"
done

for tex in "${NORMAL_TEXTURES[@]}";
do
    echo "creating ${tex}.ktx2"
    ktx create --encode uastc --generate-mipmap --assign-tf linear --format R8G8B8_UNORM --normalize --uastc-quality 3 --uastc-rdo --uastc-rdo-l 0.5 "${tex}.png" "${tex}.ktx2"
    echo "transcoding ${tex}.ktx2"
    ktx transcode --target bc7 "${tex}.ktx2" "${tex}.ktx2"
done

for tex in "${ROUGHNESS_METALNESS_TEXTURES[@]}";
do
    echo "creating ${tex}.ktx2"
    ktx create --encode uastc --generate-mipmap --assign-tf linear --format R8G8B8A8_UNORM --input-swizzle gggb --uastc-quality 3 --uastc-rdo --uastc-rdo-l 1.0 "${tex}.png" "${tex}.ktx2"
    echo "transcoding ${tex}.ktx2"
    ktx transcode --target bc5 "${tex}.ktx2" "${tex}.ktx2"
done

for tex in "${DECAL_MASK_ALPHA_TEXTURES[@]}";
do
    echo "creating ${tex}.ktx2"
    ktx create --encode uastc --generate-mipmap --assign-tf linear --format R8G8B8A8_UNORM --uastc-quality 3 --uastc-rdo --uastc-rdo-l 4.0 "${tex}.png" "${tex}.ktx2"
    echo "transcoding ${tex}.ktx2"
    ktx transcode --target bc7 "${tex}.ktx2" "${tex}.ktx2"
done
