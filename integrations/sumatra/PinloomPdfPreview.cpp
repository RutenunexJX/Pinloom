// Paired MuPDF preview helper. GPL-3.0-or-later; see reader/licenses.
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static std::string utf8(const wchar_t* text) {
    int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), count, nullptr, nullptr);
    result.pop_back();
    return result;
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--capabilities") {
        puts("{\"protocol\":\"pinloom-pdf-preview/v1\",\"pairedReader\":\"3.6.1-pinloom.2\",\"sourceRevision\":\"b793525a841a9dc4a47598060f39510c2ee140bd\"}");
        return 0;
    }
    if (argc != 8 || std::wstring(argv[1]) != L"convert" || std::wstring(argv[2]) != L"-o"
        || std::wstring(argv[4]) != L"-O") {
        fputs("Usage: PinloomPdfPreview convert -o page.png -O resolution=144 input.pdf page\n", stderr);
        return 2;
    }
    int dpi = 0, pageNumber = 0; char tail = 0;
    const auto option = utf8(argv[5]), number = utf8(argv[7]);
    if (sscanf(option.c_str(), "resolution=%d%c", &dpi, &tail) != 1
        || sscanf(number.c_str(), "%d%c", &pageNumber, &tail) != 1
        || dpi < 72 || dpi > 300 || pageNumber < 1) return 2;
    const auto input = utf8(argv[6]), output = utf8(argv[3]);
    const auto metadata = std::wstring(argv[3]) + L".json";
    fz_context* ctx = fz_new_context(nullptr, nullptr, 64 * 1024 * 1024);
    if (!ctx) return 3;
    pdf_document* document = nullptr;
    pdf_page* page = nullptr;
    fz_pixmap* pixmap = nullptr;
    FILE* json = nullptr;
    int code = 0;
    fz_var(document); fz_var(page); fz_var(pixmap); fz_var(json); fz_var(code);
    fz_try(ctx) {
        document = pdf_open_document(ctx, input.c_str());
        if (pdf_needs_password(ctx, document)) fz_throw(ctx, FZ_ERROR_ARGUMENT, "PDF password is required");
        if (pageNumber > pdf_count_pages(ctx, document)) fz_throw(ctx, FZ_ERROR_ARGUMENT, "PDF page no longer exists");
        page = pdf_load_page(ctx, document, pageNumber - 1);
        auto object = pdf_lookup_page_obj(ctx, document, pageNumber - 1);
        const auto media = pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, object, PDF_NAME(MediaBox)));
        auto cropObject = pdf_dict_get_inheritable(ctx, object, PDF_NAME(CropBox));
        const auto crop = fz_intersect_rect(cropObject ? pdf_to_rect(ctx, cropObject) : media, media);
        int rotation = pdf_to_int(ctx, pdf_dict_get_inheritable(ctx, object, PDF_NAME(Rotate)));
        rotation = (rotation % 360 + 360) % 360;
        auto unitObject = pdf_dict_get(ctx, object, PDF_NAME(UserUnit));
        float unit = unitObject ? pdf_to_real(ctx, unitObject) : 1;
        const auto bounds = fz_bound_page(ctx, &page->super);
        const auto scale = fz_scale(dpi / 72.0f, dpi / 72.0f);
        const auto pixels = fz_round_rect(fz_transform_rect(bounds, scale));
        const double width = double(pixels.x1) - pixels.x0, height = double(pixels.y1) - pixels.y0;
        if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1
            || width > 16384 || height > 16384 || width * height > 32 * 1024 * 1024)
            fz_throw(ctx, FZ_ERROR_LIMIT, "PDF page exceeds preview size limits");
        pixmap = fz_new_pixmap_from_page(ctx, &page->super, scale, fz_device_rgb(ctx), 0);
        fz_save_pixmap_as_png(ctx, pixmap, output.c_str());
        json = _wfopen(metadata.c_str(), L"wb");
        if (!json) fz_throw(ctx, FZ_ERROR_SYSTEM, "Cannot write preview geometry");
        if (fprintf(json,
            "{\"protocol\":\"pinloom-pdf-preview/v1\",\"page\":%d,\"dpi\":%d,"
            "\"mediaBox\":[%.9g,%.9g,%.9g,%.9g],\"cropBox\":[%.9g,%.9g,%.9g,%.9g],"
            "\"rotation\":%d,\"userUnit\":%.9g,\"pageBounds\":[%.9g,%.9g,%.9g,%.9g]}\n",
            pageNumber, dpi, media.x0, media.y0, media.x1, media.y1, crop.x0, crop.y0, crop.x1, crop.y1,
            rotation, unit, bounds.x0, bounds.y0, bounds.x1, bounds.y1) < 0
            || fflush(json) != 0) fz_throw(ctx, FZ_ERROR_SYSTEM, "Cannot save preview geometry");
    }
    fz_always(ctx) {
        if (json) fclose(json);
        fz_drop_pixmap(ctx, pixmap);
        fz_drop_page(ctx, page ? &page->super : nullptr);
        pdf_drop_document(ctx, document);
    }
    fz_catch(ctx) {
        fprintf(stderr, "PDF preview failed: %.240s\n", fz_caught_message(ctx));
        DeleteFileW(argv[3]); DeleteFileW(metadata.c_str());
        code = 1;
    }
    fz_drop_context(ctx);
    return code;
}
