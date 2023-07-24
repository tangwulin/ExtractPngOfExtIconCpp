#include <iostream>
#include <Windows.h>
#include <Commctrl.h>
#include <CommonControls.h>
#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <memory>
#include <vector>
#include <system_error>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>

namespace Gdiplus {
	using std::max;
	using std::min;
}  // namespace Gdiplus
#include <Gdiplus.h>
#pragma comment(lib,"gdiplus.lib")

class ComInit {
public:
	ComInit() { CoInitializeEx(0, COINIT_MULTITHREADED); }

	~ComInit() { CoUninitialize(); }

private:
	ComInit(const ComInit&);
	ComInit& operator=(const ComInit&);
};

class GdiPlusInit {
public:
	GdiPlusInit() {
		Gdiplus::GdiplusStartupInput startupInput;
		Gdiplus::GdiplusStartup(std::addressof(this->token),
			std::addressof(startupInput), nullptr);
	}

	~GdiPlusInit() { Gdiplus::GdiplusShutdown(this->token); }

private:
	GdiPlusInit(const GdiPlusInit&);
	GdiPlusInit& operator=(const GdiPlusInit&);

	ULONG_PTR token;
};

struct IStreamDeleter {
	void operator()(IStream* pStream) const { pStream->Release(); }
};

std::unique_ptr<Gdiplus::Bitmap> CreateBitmapFromIcon(
	HICON hIcon, std::vector<std::int32_t>& buffer) {
	ICONINFO iconInfo = { 0 };
	GetIconInfo(hIcon, std::addressof(iconInfo));

	BITMAP bm = { 0 };
	GetObject(iconInfo.hbmColor, sizeof(bm), std::addressof(bm));

	std::unique_ptr<Gdiplus::Bitmap> bitmap;

	if (bm.bmBitsPixel == 32) {
		auto hDC = GetDC(nullptr);

		BITMAPINFO bmi = { 0 };
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = bm.bmWidth;
		bmi.bmiHeader.biHeight = -bm.bmHeight;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		auto nBits = bm.bmWidth * bm.bmHeight;
		buffer.resize(nBits);
		GetDIBits(hDC, iconInfo.hbmColor, 0, bm.bmHeight,
			std::addressof(buffer[0]), std::addressof(bmi),
			DIB_RGB_COLORS);

		auto hasAlpha = false;
		for (std::int32_t i = 0; i < nBits; i++) {
			if ((buffer[i] & 0xFF000000) != 0) {
				hasAlpha = true;
				break;
			}
		}

		if (!hasAlpha) {
			std::vector<std::int32_t> maskBits(nBits);
			GetDIBits(hDC, iconInfo.hbmMask, 0, bm.bmHeight,
				std::addressof(maskBits[0]), std::addressof(bmi),
				DIB_RGB_COLORS);

			for (std::int32_t i = 0; i < nBits; i++) {
				if (maskBits[i] == 0) {
					buffer[i] |= 0xFF000000;
				}
			}
		}

		bitmap.reset(new Gdiplus::Bitmap(
			bm.bmWidth, bm.bmHeight, bm.bmWidth * sizeof(std::int32_t),
			PixelFormat32bppARGB,
			static_cast<BYTE*>(
				static_cast<void*>(std::addressof(buffer[0])))));

		ReleaseDC(nullptr, hDC);
	}
	else {
		bitmap.reset(Gdiplus::Bitmap::FromHICON(hIcon));
	}

	DeleteObject(iconInfo.hbmColor);
	DeleteObject(iconInfo.hbmMask);

	return bitmap;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
	UINT num = 0u;
	UINT size = 0u;

	Gdiplus::GetImageEncodersSize(std::addressof(num), std::addressof(size));

	if (size == 0u) {
		return -1;
	}

	std::unique_ptr<Gdiplus::ImageCodecInfo> pImageCodecInfo(
		static_cast<Gdiplus::ImageCodecInfo*>(
			static_cast<void*>(new BYTE[size])));

	if (pImageCodecInfo == nullptr) {
		return -1;
	}

	GetImageEncoders(num, size, pImageCodecInfo.get());

	for (UINT i = 0u; i < num; i++) {
		if (std::wcscmp(pImageCodecInfo.get()[i].MimeType, format) == 0) {
			*pClsid = pImageCodecInfo.get()[i].Clsid;
			return i;
		}
	}

	return -1;
}

std::vector<unsigned char> HIconToPNG(HICON hIcon) {
	GdiPlusInit init;

	std::vector<std::int32_t> buffer;
	auto bitmap = CreateBitmapFromIcon(hIcon, buffer);

	CLSID encoder;
	if (GetEncoderClsid(L"image/png", std::addressof(encoder)) == -1) {
		return std::vector<unsigned char>{};
	}

	IStream* tmp;
	if (CreateStreamOnHGlobal(nullptr, TRUE, std::addressof(tmp)) != S_OK) {
		return std::vector<unsigned char>{};
	}

	std::unique_ptr<IStream, IStreamDeleter> pStream{tmp};

	if (bitmap->Save(pStream.get(), std::addressof(encoder), nullptr) !=
		Gdiplus::Status::Ok) {
		return std::vector<unsigned char>{};
	}

	STATSTG stg = { 0 };
	LARGE_INTEGER offset = { 0 };

	if (pStream->Stat(std::addressof(stg), STATFLAG_NONAME) != S_OK ||
		pStream->Seek(offset, STREAM_SEEK_SET, nullptr) != S_OK) {
		return std::vector<unsigned char>{};
	}

	std::vector<unsigned char> result(
		static_cast<std::size_t>(stg.cbSize.QuadPart));
	ULONG ul;

	if (pStream->Read(std::addressof(result[0]),
		static_cast<ULONG>(stg.cbSize.QuadPart),
		std::addressof(ul)) != S_OK ||
		stg.cbSize.QuadPart != ul) {
		return std::vector<unsigned char>{};
	}

	return result;
}

std::wstring Utf8ToWide(const std::string& src) {
	/*const auto size =
		MultiByteToWideChar(CP_UTF8, 0u, src.data(), -1, nullptr, 0u);
	std::vector<wchar_t> dest(size, L'\0');

	if (MultiByteToWideChar(CP_UTF8, 0u, src.data(), -1, dest.data(),
		dest.size()) == 0) {
		throw std::system_error{static_cast<int>(GetLastError()),
			std::system_category()};
	}

	return std::wstring{dest.begin(), dest.end()};*/

	const int size = MultiByteToWideChar(CP_UTF8, 0u, src.c_str(), -1, nullptr, 0);
	std::vector<wchar_t> dest(size, L'\0');

	if (MultiByteToWideChar(CP_UTF8, 0u, src.c_str(), -1, dest.data(), size) == 0) {
		throw std::system_error{ static_cast<int>(GetLastError()), std::system_category() };
	}

	return std::wstring{ dest.data() };
}

std::wstring String2Wstring(std::string wstr)
{
	std::wstring res;
	int len = MultiByteToWideChar(CP_ACP, 0, wstr.c_str(), wstr.size(), nullptr, 0);
	if (len < 0) {
		return res;
	}
	wchar_t* buffer = new wchar_t[len + 1];
	if (buffer == nullptr) {
		return res;
	}
	MultiByteToWideChar(CP_ACP, 0, wstr.c_str(), wstr.size(), buffer, len);
	buffer[len] = '\0';
	res.append(buffer);
	delete[] buffer;
	return res;
}

std::vector<unsigned char> GetIcon(const std::wstring& name, int size, UINT flag) {
	ComInit init;

	flag |= SHGFI_ICON;

	switch (size) {
	case 16:
		flag |= SHGFI_SMALLICON;
		break;
	case 32:
		flag |= SHGFI_LARGEICON;
		break;
	case 64:
	case 256:
		flag |= SHGFI_SYSICONINDEX;
		break;
	}

	SHFILEINFOW sfi = { 0 };

	DWORD_PTR hr;
	hr = SHGetFileInfoW(name.c_str(), 0, std::addressof(sfi), sizeof(sfi), flag);

	HICON hIcon;

	if (size == 16 || size == 32) {
		hIcon = sfi.hIcon;
	}
	else {
		HIMAGELIST* imageList;
		hr = SHGetImageList(
			size == 64 ? SHIL_EXTRALARGE : SHIL_JUMBO, IID_IImageList,
			static_cast<void**>(
				static_cast<void*>(std::addressof(imageList))));

		if (FAILED(hr)) {
			return std::vector<unsigned char>{};
		}

		hr = static_cast<IImageList*>(static_cast<void*>(imageList))
			->GetIcon(sfi.iIcon, ILD_TRANSPARENT, std::addressof(hIcon));

		if (FAILED(hr)) {
			return std::vector<unsigned char>{};
		}
	}

	auto buffer = HIconToPNG(hIcon);
	DestroyIcon(hIcon);
	return buffer;
}

std::string GetFullPath(const std::string& path)
{
	char buffer[MAX_PATH];
	if (GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr) == 0)
	{
		return "";
	}
	return std::string(buffer);
}

bool DirectoryExists(const std::string& path)
{
	DWORD fileAttributes = GetFileAttributesA(path.c_str());
	return (fileAttributes != INVALID_FILE_ATTRIBUTES &&
		(fileAttributes & FILE_ATTRIBUTE_DIRECTORY));
}

void CreateDirectoryIfNotExists(const std::string& path)
{
	if (!DirectoryExists(path))
	{
		CreateDirectoryA(path.c_str(), nullptr);
	}
}

std::string GetFileExtension(const std::string& filePath)
{
	// 查找最后一个点号的位置
	size_t dotPos = filePath.find_last_of('.');
	if (dotPos != std::string::npos)
	{
		// 使用 substr 函数提取点号后面的部分作为扩展名
		return filePath.substr(dotPos + 1);
	}
	else
	{
		// 如果没有找到点号，则返回空字符串表示没有扩展名
		return "";
	}
}

int Process(const std::wstring& filePath, const std::string& outputPath, bool std_out)
{
	// 在这里实现处理逻辑
	GdiPlusInit gdiPlusInit;
	std::vector<unsigned char> pngBuffer;
	pngBuffer = GetIcon(filePath, 256, 0);

	//std::cout << pngBuffer.size() << std::endl;
	// 打开一个二进制文件输出流
	std::ofstream outFile(outputPath, std::ios::binary);

	// 检查文件是否成功打开
	if (!outFile) {
		std::cerr << "无法打开文件 output.png" << std::endl;
		//return 1;  返回非零值表示文件打开失败
	}

	// 将data向量的内容写入到文件中
	outFile.write(reinterpret_cast<char*>(pngBuffer.data()), pngBuffer.size());

	// 关闭文件流
	outFile.close();

	std::cout << pngBuffer.size() << std::endl;
	return pngBuffer.size();
	//return 0; 返回零表示写入成功
}

int main(int argc, char* argv[])
{
	//GdiPlusInit gdiPlusInit;
	if (argc == 1)
	{
		std::cout << "Usage: ExtractPngOfExtIcon.exe -file path\\to\\file -O path\\to\\output"
			<< "\n -file: file path"
			<< "\n -O: output folder"
			//<< "\n -stdout: output the png data in base64 url"
			//<< "\n -batch: process several extensions in one time,use ',' to divide them"
			<< std::endl;
		return 0;
	}

	// 定义默认的文件扩展名和输出文件路径
	std::string filePath;
	std::string outputPath = ".";
	bool std_out = false;

	// 解析命令行参数
	for (int i = 0; i < argc; i++)
	{
		if (std::string(argv[i]) == "-file") {
			// 如果找到了 "-ext" 参数标志，则获取下一个参数作为文件路径
			if (i + 1 < argc) {
				filePath = argv[i + 1];
			}
		}
		else if (std::string(argv[i]) == "-O")
		{
			// 如果找到了 "-O" 参数标志，则获取下一个参数作为输出文件路径
			if (i + 1 < argc)
			{
				// 如果路径是相对路径，则转换为绝对路径
				outputPath = GetFullPath(argv[i + 1]);
				CreateDirectoryIfNotExists(outputPath);
				auto ext = GetFileExtension(filePath);
				if (ext == "exe") {
					std::filesystem::path path = filePath;
					// 提取文件名
					std::string fileName = path.filename().string();
					outputPath = outputPath + "\\" + fileName + ".png";
				}
				else {
					outputPath = outputPath + "\\" + GetFileExtension(filePath) + ".png";
				}

				//outputPath = GetFileExtension(filePath) + ".png";
			}

		}
		else if (std::string(argv[i]) == "-stdout")
		{
			std_out = true;
		}

	}
	std::cout << outputPath << std::endl;
	//Process(filePath, outputPath, std_out);
	// 将 std::string 类型的 filePath 转换为 std::wstring 类型
	//std::wstring wFilePath = Utf8ToWide(filePath);
	std::wstring wFilePath = String2Wstring(filePath);
	// 调用 Process 函数时传入 std::wstring 类型的文件路径
	Process(wFilePath, outputPath, std_out);

	return 0;
}
