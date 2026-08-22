#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "attadipa_icons.h"

// The generated image assets, and the one thing that resolves a token and a
// panel into one of them (T-034).
//
// The pipeline's *inputs* are checked in Python — `tools/assets/selftest.py`
// proves the refusals refuse, and `generate_images.py --check` proves the tree
// is not stale. What is left, and what only C++ can see, is whether the bytes
// that were linked are an image: a descriptor with the format, dimensions and
// stride the header claims, and pixel data that is a drawing rather than a
// blank rectangle. A mask that generated as all-zero would compile, link, draw
// nothing, and pass every check upstream of this file.

using namespace attadipa;
using namespace attadipa::ui;

namespace {

int failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// The two panels, by density. Same reasoning as `test_ui_tokens.cpp`: a test
// that named a board would be a test that could not notice the two boards
// landing on the same pixel size, which is the property this file exists for.
constexpr std::uint16_t kTWatchDpi = 261;
constexpr std::uint16_t kWaveshareDpi = 315;

struct Listed {
    int                   pixels;
    const lv_image_dsc_t* dsc;
    const char*           symbol;
};

// Everything the generator says it produced, straight from its own list.
const std::vector<Listed>& listed()
{
    static const std::vector<Listed> v = {
#define ATTADIPA_TEST_ENTRY(name_token, px, symbol) Listed{(px), &(symbol), #symbol},
        ATTADIPA_ICON_LIST(ATTADIPA_TEST_ENTRY)
#undef ATTADIPA_TEST_ENTRY
    };
    return v;
}

// How much of a mask is ink, in hundredths, counting a pixel as ink when it is
// more than half opaque.
int ink_centi(const lv_image_dsc_t& d)
{
    const auto* bytes = static_cast<const std::uint8_t*>(d.data);
    long        on = 0;
    for (std::uint32_t y = 0; y < d.header.h; ++y) {
        for (std::uint32_t x = 0; x < d.header.w; ++x) {
            if (bytes[y * d.header.stride + x] > 127) {
                ++on;
            }
        }
    }
    return static_cast<int>(on * 10000 / (d.header.w * d.header.h));
}

bool has_partial_alpha(const lv_image_dsc_t& d)
{
    const auto* bytes = static_cast<const std::uint8_t*>(d.data);
    for (std::uint32_t i = 0; i < d.data_size; ++i) {
        if (bytes[i] > 16 && bytes[i] < 200) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main()
{
    // ---------------------------------------------------------------------
    // Every linked asset is an A8 mask that describes itself correctly.
    //
    // `stride == w` is not a formality: LVGL reads a row at a time using the
    // stride, so a descriptor whose stride disagreed with its width would draw
    // a sheared icon rather than fail, and a sheared icon at 33 px looks like
    // an icon somebody drew badly.
    CHECK(!listed().empty());
    for (const Listed& l : listed()) {
        CHECK(l.dsc != nullptr);
        CHECK(l.dsc->header.magic == LV_IMAGE_HEADER_MAGIC);
        CHECK(l.dsc->header.cf == LV_COLOR_FORMAT_A8);
        CHECK(static_cast<int>(l.dsc->header.w) == l.pixels);
        CHECK(static_cast<int>(l.dsc->header.h) == l.pixels);
        CHECK(static_cast<int>(l.dsc->header.stride) == l.pixels);
        CHECK(l.dsc->data != nullptr);
        CHECK(l.dsc->data_size ==
              static_cast<std::uint32_t>(l.pixels) * static_cast<std::uint32_t>(l.pixels));
    }

    // ---------------------------------------------------------------------
    // Each mask is a drawing.
    //
    // The bands are wide on purpose — this is not a pixel comparison against a
    // reference, which would fail on every deliberate redraw and teach the next
    // person to regenerate the reference without looking. It fails on the two
    // things that are always wrong: nothing drawn, and everything drawn.
    for (const Listed& l : listed()) {
        const int ink = ink_centi(*l.dsc);
        check(ink > 400, "a mask with under 4% ink is not a drawing", __LINE__);
        check(ink < 6000, "a mask with over 60% ink is a filled rectangle", __LINE__);
        check(has_partial_alpha(*l.dsc),
              "no partial alpha: the mask was not antialiased", __LINE__);
    }

    // ---------------------------------------------------------------------
    // The lookup resolves a token and a density, and nothing else.
    const Metrics t_watch = Metrics::for_dpi(kTWatchDpi);
    const Metrics waveshare = Metrics::for_dpi(kWaveshareDpi);

    // The thesis of the whole pipeline, in one assertion: a pixel size is a
    // pixel size. `icon.size.lg` on the smaller, denser-per-token panel and
    // `icon.size.md` on the larger one are both 39 px, so they are the same
    // file — not two files with different names, and certainly not a lookup
    // that had to be told which board it was on.
    CHECK(t_watch.px(dp_of(IconSize::Lg)) == 39);
    CHECK(waveshare.px(dp_of(IconSize::Md)) == 39);
    CHECK(assets::icon(assets::Icon::Mesh, IconSize::Lg, t_watch) ==
          assets::icon(assets::Icon::Mesh, IconSize::Md, waveshare));
    CHECK(assets::icon(assets::Icon::Mesh, IconSize::Lg, t_watch) != nullptr);

    // Every icon resolves at every size the manifest generates, on whichever
    // panel asks for it.
    for (const assets::Icon which :
         {assets::Icon::Mesh, assets::Icon::Position, assets::Icon::Warning}) {
        CHECK(assets::icon(which, IconSize::Md, t_watch) != nullptr);       // 33
        CHECK(assets::icon(which, IconSize::Lg, t_watch) != nullptr);       // 39
        CHECK(assets::icon(which, IconSize::Md, waveshare) != nullptr);     // 39
        CHECK(assets::icon(which, IconSize::Lg, waveshare) != nullptr);     // 47
        CHECK(assets::name_of(which) != nullptr);
    }

    // ---------------------------------------------------------------------
    // A size with no asset is `nullptr` and never a substitute.
    //
    // This is final §86 arriving at the layer that could most easily break it.
    // `icon.size.sm` is 26 px on the T-Watch and 32 on the Waveshare, and
    // `icon.size.xl` is 52 and 63 — none of the four is generated, and the
    // tempting thing for a lookup to do is hand back the nearest one it has.
    // A caller drawing that would be showing a picture nobody drew for that
    // panel, which is the exact failure the rule exists to prevent, one layer
    // later.
    CHECK(t_watch.px(dp_of(IconSize::Sm)) == 26);
    CHECK(waveshare.px(dp_of(IconSize::Xl)) == 63);
    for (const assets::Icon which :
         {assets::Icon::Mesh, assets::Icon::Position, assets::Icon::Warning}) {
        CHECK(assets::icon(which, IconSize::Sm, t_watch) == nullptr);
        CHECK(assets::icon(which, IconSize::Sm, waveshare) == nullptr);
        CHECK(assets::icon(which, IconSize::Xl, t_watch) == nullptr);
        CHECK(assets::icon(which, IconSize::Xl, waveshare) == nullptr);
    }
    CHECK(assets::icon_px(assets::Icon::Mesh, 40) == nullptr);
    CHECK(assets::icon_px(assets::Icon::Mesh, 0) == nullptr);
    CHECK(assets::icon_px(assets::Icon::Mesh, -39) == nullptr);

    // ---------------------------------------------------------------------
    // The icons are different pictures.
    //
    // Three lookups returning one descriptor would satisfy every assertion
    // above. They are distinct assets and their ink differs, which is the
    // cheapest available proof that `warning` is not `mesh` with another name.
    std::set<const lv_image_dsc_t*> distinct;
    std::set<int>                   inks;
    for (const assets::Icon which :
         {assets::Icon::Mesh, assets::Icon::Position, assets::Icon::Warning}) {
        const lv_image_dsc_t* d = assets::icon(which, IconSize::Lg, t_watch);
        distinct.insert(d);
        inks.insert(ink_centi(*d));
    }
    CHECK(distinct.size() == 3);
    CHECK(inks.size() == 3);

    // Every symbol the generator listed is named once. A list with a duplicate
    // would silently drop an asset from the table without dropping it from the
    // build.
    std::set<std::string> names;
    for (const Listed& l : listed()) {
        names.insert(l.symbol);
    }
    CHECK(names.size() == listed().size());

    if (failures == 0) {
        std::printf("ui_icons: %zu asset(s), all A8, all drawn, no substitution\n",
                    listed().size());
    }
    return failures == 0 ? 0 : 1;
}
