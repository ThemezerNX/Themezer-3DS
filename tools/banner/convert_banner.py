# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
# Converts banner.gltf to the HOME Menu banner CGFX with pycgfx, then gives it the structure of hardware-tested banners
# (github.com/Epic0522/ClouDS-Music-FA, whose banner.cgfx is the reference):
# - every mesh bound to its node by name (pycgfx leaves mesh_node_name empty)
# - matte materials: pycgfx's PBR conversion adds a strong white specular term that washes the banner out under the
#   HOME Menu's light, so the specular stage is bypassed and diffuse lighting stays
# - a lighting floor on the logo: its texture mixed back in at LIGHT_FLOOR, so the rounded edge doesn't go dark where
#   it turns away from the light (--no-light-floor leaves it out)
# - the wordmark ("name") unlit, straight from its texture, and a Y-axis billboard; with --name-uv-zoom Z its
#   materials show only the middle Z of their texture (make_banner.py --uv-zoom Z draws the letters for that)
# - with make_banner.py --spin, the logo's spin as a plain Y angle, clockwise seen from above (pycgfx turns the
#   quaternions into Euler angles that flip past 90 degrees)
# - the icon art and the wordmark layers as ETC1A4 instead of pycgfx's RGBA4, whose 16 levels per channel put visible
#   stripes in the gradient. Not plain ETC1: the HOME Menu scrambles that (blocks out of place, tested on hardware),
#   while ETC1A4 shows correctly. --rgba4-logo keeps the icon art in RGBA4.
# - no skinning, no bone index or weight streams, and only ordinary transform animation members with every channel a
#   curve (pycgfx marks unanimated channels as ignored, which the reference never does)
# - with --name-la8, the wordmark layers as LA8 (white, alpha) instead, as the HOME Menu hangs when it filters ETC1A4,
#   and the fill takes its colour from its vertex colours
# usage: convert_banner.py <pycgfx dir> <banner.gltf> <banner.cgfx> [--rgba4-logo] [--no-light-floor] [--name-uv-zoom Z]
#        [--name-la8] [--name-like-2d-banner]
# - with --name-like-2d-banner, the wordmark's textures set up exactly like bannertool's 2D banner, which the HOME Menu
#   filters smoothly: RGBA4 with its GL format and type filled in, texture parameters 0x2206 (linear both ways, no
#   bit 0; pycgfx writes its filters one bit too low, which leaves bit 0 set) and a linear minification filter. Every
#   sampler also gets the LOD bias that ends the record, which pycgfx leaves out (the console reads 4 bytes too far).
# - every texture's data on a 128-byte boundary of the file, as in bannertool's and ClouDS's banners (pycgfx uses 16;
#   the HOME Menu hung whenever it filtered such a texture linearly)
# - with --name-linear, the wordmark's textures filtered linearly both ways (texture parameters 0x6, clamped to the
#   edge); --logo-linear does the same for the icon art and its side
import math, os, sys
sys.path.insert(0, sys.argv[1])
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
import etc1
import gltflib
import main as pycgfx
import struct as _struct
from cgfx import mtob as _mtob
from cgfx.shared import Reference as _Reference

# the full standard sampler record (SPICA's GfxTextureSamplerStd): type, owner, min filter, border colour, LOD bias
_mtob.TextureSampler.struct = _struct.Struct("Iiiffff" + "f")
_mtob.TextureSampler.lod_bias = 0.0
_mtob.TextureSampler.values = lambda self: (self.type, _Reference(self.owner), self.min_filter, self.border_color, self.lod_bias)
from cgfx.shared import StringTable as _StringTable


class _ImagTable(_StringTable):
    # every IMAG entry padded to a multiple of 128 bytes, so each one starts on a 128-byte boundary of the table
    @staticmethod
    def correct(s):
        return s.encode() + b"\0" if isinstance(s, str) else s + b"\0" * (-len(s) % 128)


class _Strings(_StringTable):
    # padded so the IMAG header after it ends on a 128-byte boundary of the file
    def prepare(self, offset):
        self.offset = offset
        self.padding = -(offset + self.total + 8) % 128
        self.total += self.padding
        return offset + self.total


def write_aligned(cgfx):
    # pycgfx's write() with the two tables above
    strings, imag = _Strings(), _ImagTable()
    offset = cgfx.prepare(0, strings, imag)
    offset = strings.prepare(offset)
    cgfx.data.section_size = offset - cgfx.data.offset
    assert not imag.empty()
    cgfx.header.nr_blocks = 2
    offset = imag.prepare(offset + 8)
    cgfx.header.file_size = offset
    data = cgfx.write(strings, imag) + strings.write()
    return data + b"IMAG" + imag.size().to_bytes(4, "little") + imag.write()


from cgfx.shared import ColorFloat, Matrix, Vector4
from cgfx.mtob import FragmentLightingFlags, ConstantColorSource
from cgfx.canm import CANMBoneTransform, PrimitiveType, FloatAnimationCurve
from cgfx.primitives import VertexAttributeUsage
from cgfx.sobj import BillboardMode
from cgfx.txob import TextureFormat

src, dst = sys.argv[2], sys.argv[3]
LIGHT_FLOOR = None if '--no-light-floor' in sys.argv[4:] else 0.6
NAME_UV_ZOOM = float(sys.argv[sys.argv.index('--name-uv-zoom') + 1]) if '--name-uv-zoom' in sys.argv else None
NAME_LA8 = '--name-la8' in sys.argv[4:]
NAME_LIKE_2D = '--name-like-2d-banner' in sys.argv[4:]
NAME_LINEAR = '--name-linear' in sys.argv[4:]
LOGO_LINEAR = '--logo-linear' in sys.argv[4:]
cgfx = pycgfx.convert_gltf(gltflib.GLTF.load(src, load_file_resources=True))
model = cgfx.data.models['COMMON']
assert model is not None, 'no COMMON model'
bones = set(model.skeleton.bones)

for mesh in model.meshes.data.contents:
    node = mesh.name.split('_')[0]
    assert node in bones, (mesh.name, bones)
    mesh.mesh_node_name = node

model.skeleton.bones['name'].billboard_mode = BillboardMode.YAxial
assert model.skeleton.bones['world'] is not None

for name in model.materials:
    m = model.materials[name]
    assert m.used_texture_coordinates_count == 1, (name, 'every material should be textured')
    m.material_color.constant[0] = ColorFloat(0, 0, 0, 1)
    stages = m.fragment_shader.texture_combiners
    if NAME_LINEAR if name.startswith('name') else LOGO_LINEAR:
        for mapper in m.texture_mappers:
            if mapper is None: continue
            mapper.commands[2].head = 0x0006  # linear magnification (bit 1) and minification (bit 2), clamped
            mapper.sampler.min_filter = 1  # linear
    stages[2].src_rgb, stages[2].combine_rgb = 0xFFF, 0  # previous stage, no specular added
    m.fragment_shader.fragment_lighting.flags = FragmentLightingFlags(0)
    m.fragment_shader.fragment_lighting_table.distribution_0_sampler = None
    if LIGHT_FLOOR is not None and not name.startswith('name'):
        # third stage: texture * LIGHT_FLOOR + lit * (1 - LIGHT_FLOOR), the constant giving LIGHT_FLOOR
        m.material_color.constant[0] = ColorFloat(LIGHT_FLOOR, LIGHT_FLOOR, LIGHT_FLOOR, 1)
        stages[2].constant = ConstantColorSource.Constant0
        stages[2].src_rgb, stages[2].combine_rgb = 0xEF3, 4  # interpolate(texture 0, previous, constant)
    if name.startswith('name'):
        stages[0].src_rgb, stages[0].combine_rgb = 0x003, 0  # texture 0 as is
        stages[0].src_alpha, stages[0].combine_alpha = 0x003, 0
        if NAME_LIKE_2D:
            for mapper in m.texture_mappers:
                if mapper is None: continue
                mapper.commands[2].head = 0x2206  # the texture parameter register, as bannertool's banner
                mapper.sampler.min_filter = 1
                mapper.sampler.border_color = ColorFloat(0, 0, 0, 1)
        if (NAME_LA8 or NAME_LIKE_2D) and name == 'name_fill':
            stages[0].src_rgb, stages[0].combine_rgb = 0x030, 1  # vertex colour times the (white) texture
            stages[0].src_alpha, stages[0].combine_alpha = 0x030, 1
        stages[1].src_rgb, stages[1].combine_rgb = 0xFFF, 0  # no lighting
        if NAME_UV_ZOOM:
            # texture coordinates scaled about the middle: the runtime uses this matrix as is (should_generate_matrix off)
            z, t = NAME_UV_ZOOM, (1 - NAME_UV_ZOOM) / 2
            coord = m.texture_coordinators[0]
            assert not coord.should_generate_matrix
            coord.transform_matrix = Matrix(Vector4(z, 0, 0, t), Vector4(0, z, 0, t), Vector4(0, 0, 1, 0))

for shape in model.shapes.data.contents:
    for ps in shape.primitive_sets.data.contents:
        assert ps.skinning_mode == 0, (shape.name, ps.skinning_mode)
    for attr in shape.vertex_attributes.data.contents:
        for usage in attr.vertex_streams.data.contents if hasattr(attr, 'vertex_streams') else (attr,):
            assert usage.usage not in (VertexAttributeUsage.BoneIndex, VertexAttributeUsage.BoneWeight), shape.name

anim = cgfx.data.skeletal_animations['COMMON']
static = anim is None
members = {} if static else {n: anim.member_animations_data[n] for n in anim.member_animations_data}
if not static:
    assert set(members) == {'logo'}, set(members)
    spin = members['logo']
    assert isinstance(spin, CANMBoneTransform) and spin.primitive_type == PrimitiveType.Transform
    channels = ('scale_x', 'scale_y', 'scale_z', 'rot_x', 'rot_y', 'rot_z', 'pos_x', 'pos_y', 'pos_z')
    assert all(isinstance(getattr(spin, c), FloatAnimationCurve) for c in channels), [c for c in channels if not isinstance(getattr(spin, c), FloatAnimationCurve)]
    keys_x, keys_y, keys_z = (c.segments[0].keys for c in (spin.rot_x, spin.rot_y, spin.rot_z))
    last = keys_y[-1].frame
    for kx, ky, kz in zip(keys_x, keys_y, keys_z):
        kx.value, ky.value, kz.value = 0.0, -2 * math.pi * ky.frame / last, 0.0

NAME_TEX = ['name_outline.png', 'name_fill.png']
ETC = ([] if NAME_LA8 or NAME_LIKE_2D else NAME_TEX) + ([] if '--rgba4-logo' in sys.argv[4:] else ['logo.png'] + (['logo_back.png'] if 'logo_back.png' in cgfx.data.textures else []))
for name in cgfx.data.textures:
    if NAME_LIKE_2D and name in NAME_TEX:
        # RGBA4 like bannertool's texture, alpha rounded to its 16 levels (pycgfx keeps the top 4 bits)
        txob = cgfx.data.textures[name]
        img = Image.open(os.path.join(os.path.dirname(src), name)).convert('RGBA')
        px = img.load()
        for y in range(img.height):
            for x in range(img.width):
                r, g, b, a = px[x, y]
                px[x, y] = tuple(min(255, (v * 15 + 127) // 255 * 17) for v in (r, g, b, a))
        txob.hw_format = TextureFormat.RGBA4
        txob.gl_format, txob.gl_type = 0x6752, 0x8033
        from cgfx import swizzler
        txob.pixel_based_image.data = swizzler.swizzle(img.transpose(Image.Transpose.FLIP_TOP_BOTTOM), TextureFormat.RGBA4)
        continue
    if name not in ETC and not (NAME_LA8 and name in NAME_TEX): continue
    txob = cgfx.data.textures[name]
    img = Image.open(os.path.join(os.path.dirname(src), name)).convert('RGBA')
    assert img.size == (txob.width, txob.height), (name, img.size)
    if name in ETC:
        txob.hw_format = TextureFormat.ETC1A4
        txob.pixel_based_image.data = etc1.encode(img, True)
    else:
        txob.hw_format = TextureFormat.LA8
        txob.pixel_based_image.data = etc1.encode_la8(img)
assert set(ETC) <= set(cgfx.data.textures), list(cgfx.data.textures)

data = write_aligned(cgfx)
assert len(data) < 512 * 1024
i = 0
while (i := data.find(b'TXOB', i)) >= 0:
    if _struct.unpack_from('<I', data, i - 4)[0] == 0x20000011:
        pbi = i + 52 + _struct.unpack_from('<i', data, i + 52)[0]
        at = pbi + 12 + _struct.unpack_from('<i', data, pbi + 12)[0]
        assert at % 128 == 0, (hex(at), 'texture data off a 128-byte boundary')
    i += 4
open(dst, 'wb').write(data)
print(f'{dst}: {len(data)} bytes; meshes bound to', sorted({m.mesh_node_name for m in model.meshes.data.contents}),
      '; materials', list(model.materials), '; no animation' if static else f'; spin keys {len(keys_y)} over {last} frames, all 9 channels keyed')
