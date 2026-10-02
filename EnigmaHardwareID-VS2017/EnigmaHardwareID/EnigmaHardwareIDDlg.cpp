
// EnigmaHardwareIDDlg.cpp — 实现文件（现代化重构，VS2017+ / C++17）
// ============================================================
// 原始代码：Visual C++ 6.0 (1998)
// 目标编译器：Visual Studio 2017 / 2019 / 2022（Win32 / x86）
//
// 变更记录：
//   [M01] 移除手动 typedef int8_t/uint8_t 等 → #include <cstdint>
//   [M02] std::auto_ptr  → std::unique_ptr（C++17 已删除 auto_ptr）
//   [M03] for 循环变量作用域修正（VC6 非标准扩展 → 标准 C++11）
//   [M04] GetMotherboard 多处 strcat → std::string 拼接（缓冲区安全）
//   [M05] printf → spdlog 结构化日志（文件滚动 + VS 输出窗口）
//   [M06] sprintf/strcpy → sprintf_s/strcpy_s（CRT 安全函数）
//   [M07] 数组初始化 for 循环 → memset
//   [M08] malloc + 裸指针 → std::unique_ptr<BYTE[]>（GetMotherboard）
//   [M09] 重复逻辑块 → C++11 Lambda（OnDecodehwid / OnGenhwid）
//   [asm] 内联汇编 _asm 块保留不变（VS2017 Win32/x86 仍支持）
// ============================================================

#include "stdafx.h"
#include "EnigmaHardwareID.h"
#include "EnigmaHardwareIDDlg.h"
#include "config.h"
#include "logger.h"

#include <windows.h>
#include <winioctl.h>
#include <cstdint>      // [M01] uint8_t, int8_t, uint16_t, uint32_t
#include <cstring>      // memcpy, memset, strlen
#include <memory>       // std::unique_ptr
#include <string>       // std::string
#include <sstream>      // std::istringstream
#include <iomanip>

// ── 存储设备查询结构（兼容旧 SDK 版本的兜底定义）──────────
typedef enum _STORAGE_PROPERTY_ID {
    StorageDeviceProperty = 0,
    StorageAdapterProperty,
    StorageDeviceIdProperty
} STORAGE_PROPERTY_ID, *PSTORAGE_PROPERTY_ID;

typedef enum _STORAGE_QUERY_TYPE {
    PropertyStandardQuery = 0,
    PropertyExistsQuery,
    PropertyMaskQuery,
    PropertyQueryMaxDefined
} STORAGE_QUERY_TYPE, *PSTORAGE_QUERY_TYPE;

typedef struct _STORAGE_PROPERTY_QUERY {
    int PropertyId;
    int QueryType;
    UCHAR AdditionalParameters[1];
} STORAGE_PROPERTY_QUERY, *PSTORAGE_PROPERTY_QUERY;

typedef struct _STORAGE_DESCRIPTOR_HEADER {
    DWORD Version;
    DWORD Size;
} STORAGE_DESCRIPTOR_HEADER, *PSTORAGE_DESCRIPTOR_HEADER;

#ifndef IOCTL_STORAGE_QUERY_PROPERTY
#  define IOCTL_STORAGE_QUERY_PROPERTY \
     CTL_CODE(IOCTL_STORAGE_BASE, 0x0500, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifdef _DEBUG
#  define new DEBUG_NEW
#endif

/////////////////////////////////////////////////////////////////////////////
// CEnigmaHardwareIDDlg 对话框

CEnigmaHardwareIDDlg::CEnigmaHardwareIDDlg(CWnd* pParent /*=nullptr*/)
    : CDialog(CEnigmaHardwareIDDlg::IDD, pParent)
    , a(0), b(0), swap(0)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    memset(m_sBox, 0, sizeof(m_sBox));
}

void CEnigmaHardwareIDDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CEnigmaHardwareIDDlg, CDialog)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_BN_CLICKED(IDC_GETBUTTON,           OnGetbutton)
    ON_BN_CLICKED(IDC_GETTRIALKEY,         OnGettrialkey)
    ON_BN_CLICKED(IDC_EXTRACTRC4KEYFROMRSA,OnExtractrc4keyfromrsa)
    ON_BN_CLICKED(IDC_DECODEHWID,          OnDecodehwid)
    ON_BN_CLICKED(IDC_GENHWID,             OnGenhwid)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// 消息处理函数

BOOL CEnigmaHardwareIDDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    ((CButton*)GetDlgItem(IDC_COMPNCB)) ->SetCheck(BST_CHECKED);
    ((CButton*)GetDlgItem(IDC_CPUTCB))  ->SetCheck(BST_CHECKED);
    ((CButton*)GetDlgItem(IDC_SVSNCB))  ->SetCheck(BST_CHECKED);
    ((CButton*)GetDlgItem(IDC_MOTHERCB))->SetCheck(BST_CHECKED);
    ((CButton*)GetDlgItem(IDC_SVNCB))   ->SetCheck(BST_CHECKED);
    ((CButton*)GetDlgItem(IDC_WSNCB))   ->SetCheck(BST_CHECKED);
    ((CButton*)GetDlgItem(IDC_HDDSNCB)) ->SetCheck(BST_CHECKED);
    ((CButton*)GetDlgItem(IDC_USERNCB)) ->SetCheck(BST_CHECKED);

    CheckDlgButton(IDC_CHECKUSED,  BST_CHECKED);
    CheckDlgButton(IDC_INSERTSEP,  BST_CHECKED);

    spdlog::info("OnInitDialog: 对话框初始化完成，所有组件默认勾选");
    return TRUE;
}

void CEnigmaHardwareIDDlg::OnPaint()
{
    if (IsIconic()) {
        CPaintDC dc(this);
        SendMessage(WM_ICONERASEBKGND, (WPARAM)dc.GetSafeHdc(), 0);
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width()  - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;
        dc.DrawIcon(x, y, m_hIcon);
    } else {
        CDialog::OnPaint();
    }
}

HCURSOR CEnigmaHardwareIDDlg::OnQueryDragIcon()
{
    return (HCURSOR)m_hIcon;
}

/////////////////////////////////////////////////////////////////////////////
// 辅助函数

static unsigned char hextobyte(unsigned char ch)
{
    if (ch >= '0' && ch <= '9') return static_cast<unsigned char>(ch - '0');
    if (ch >= 'A' && ch <= 'F') return static_cast<unsigned char>(ch - 'A' + 10);
    if (ch >= 'a' && ch <= 'f') return static_cast<unsigned char>(ch - 'a' + 10);
    return 0;
}

/////////////////////////////////////////////////////////////////////////////
// GetHDDString — 获取硬盘型号与序列号
// [M03] for 循环变量作用域修正（3 处）
// [M06] strncpy → strncpy_s，strncat → strncat_s
// [M07] 数组初始化 for 循环 → memset

char HDDStr[MAX_PATH + 3];

void GetHDDString()
{
    HDDStr[0] = '\0';
    char winpath[MAX_PATH + 3];
    GetWindowsDirectoryA(winpath, sizeof(winpath));

    char CPath[MAX_PATH + 3];
    strcpy_s(CPath, sizeof(CPath), "\\\\.\\");           // [M06]
    size_t prefixLen    = strlen(CPath);
    CPath[prefixLen]    = winpath[0];
    CPath[prefixLen+1]  = winpath[1];
    CPath[prefixLen+2]  = '\0';

    spdlog::debug("GetHDDString: 打开存储设备路径 '{}'", CPath);

    HANDLE HandleC = CreateFileA(CPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, 0, NULL);
    if (HandleC == INVALID_HANDLE_VALUE) {
        spdlog::warn("GetHDDString: CreateFile 失败，错误码={}", GetLastError());
        return;
    }

    STORAGE_PROPERTY_QUERY spq{};
    spq.PropertyId = StorageDeviceProperty;
    spq.QueryType  = PropertyStandardQuery;

    char buffer[10000];
    DWORD BytesReturned = 0;
    if (!DeviceIoControl(HandleC, IOCTL_STORAGE_QUERY_PROPERTY,
                         &spq, sizeof(spq),
                         buffer, sizeof(buffer),
                         &BytesReturned, NULL)) {
        spdlog::warn("GetHDDString: DeviceIoControl 失败，错误码={}", GetLastError());
        CloseHandle(HandleC);
        return;
    }
    if (!BytesReturned) {
        spdlog::warn("GetHDDString: DeviceIoControl 返回 0 字节");
        CloseHandle(HandleC);
        return;
    }

    // 校验偏移量 1
    int Offset1 = 0;
    memcpy(&Offset1, buffer + 0x10, sizeof(int));
    if (Offset1 < 0 || static_cast<DWORD>(Offset1) >= BytesReturned) {
        spdlog::warn("GetHDDString: Offset1={} 越界（BytesReturned={}）",
                     Offset1, BytesReturned);
        CloseHandle(HandleC);
        return;
    }
    strncpy_s(HDDStr, sizeof(HDDStr), buffer + Offset1, _TRUNCATE);  // [M06]

    // 校验偏移量 2
    int Offset2 = 0;
    memcpy(&Offset2, buffer + 0x18, sizeof(int));
    if (Offset2 < 0 || static_cast<DWORD>(Offset2) >= BytesReturned) {
        spdlog::warn("GetHDDString: Offset2={} 越界（BytesReturned={}）",
                     Offset2, BytesReturned);
        CloseHandle(HandleC);
        return;
    }

    char SecondString[10000];
    strncpy_s(SecondString, sizeof(SecondString),
              buffer + Offset2, _TRUNCATE);               // [M06]

    if (SecondString[0] == '3' &&
        SecondString[1] >= '0' && SecondString[1] <= '9') {

        char bytes_values[10000];
        memset(bytes_values, 0, sizeof(bytes_values));    // [M07]

        int bytes_pos = 0;
        int skipped   = 0;
        int StringLen = static_cast<int>(strlen(SecondString));
        for (int i = 0; i < StringLen; i++) {             // [M03]
            if ((SecondString[i] >= '0' && SecondString[i] <= '9') ||
                (SecondString[i] >= 'a' && SecondString[i] <= 'f')) {
                if ((i - skipped) % 2 == 1)
                    bytes_values[bytes_pos] = static_cast<char>(
                        bytes_values[bytes_pos] << 4);
                bytes_values[bytes_pos] += static_cast<char>(
                    hextobyte(SecondString[i]));
                if ((i - skipped) % 2 == 1)
                    bytes_pos++;
            } else {
                skipped++;
            }
        }

        int StringLen2 = static_cast<int>(strlen(bytes_values));
        for (int i = 0; i < StringLen2; i++) {            // [M03]
            if (!((bytes_values[i] >= '0' && bytes_values[i] <= '9') ||
                  (bytes_values[i] >= 'a' && bytes_values[i] <= 'f'))) {
                bytes_values[i] = '\0';
                break;
            }
        }

        StringLen2 = static_cast<int>(strlen(bytes_values));
        for (int i = 0; i < StringLen2; i += 2) {        // [M03] 字符互换
            char firstChar  = bytes_values[i];
            char secondChar = bytes_values[i + 1];
            if (secondChar != '\0') {
                bytes_values[i]     = secondChar;
                bytes_values[i + 1] = firstChar;
            }
        }
        strncat_s(HDDStr, sizeof(HDDStr), bytes_values, _TRUNCATE);
    } else {
        strncat_s(HDDStr, sizeof(HDDStr), SecondString, _TRUNCATE);
    }

    spdlog::info("GetHDDString 完成，HDDStr='{}'", HDDStr);
    CloseHandle(HandleC);
}

/////////////////////////////////////////////////////////////////////////////
// GetCPUVendorID — 通过 CPUID 指令获取 CPU 信息
// [asm] 内联汇编保留不变（VS2017 Win32/x86 目标仍完整支持）
// 注意：x64 构建不支持 _asm，项目必须保持 Win32（x86）目标

char CPUVendor[0x258];
char CPUVendorP2[0x58];
static const char AMDCPU[] = "AuthenticAMD";

void GetCPUVendorID()
{
    spdlog::debug("GetCPUVendorID: 执行 CPUID 指令序列");

    unsigned int KeepAddress = 0;
    _asm
    {
        MOV EAX,0x0
        CPUID
        MOV DWORD PTR CPUVendor[0],  EBX
        MOV DWORD PTR CPUVendor[4],  EDX
        MOV DWORD PTR CPUVendor[8],  ECX
        MOV EAX,0x1
        CPUID
        MOV EBX,EAX
        AND EAX,0xF
        MOV DWORD PTR CPUVendor[0x0C],EAX
        SHR EBX,0x4
        MOV EAX,EBX
        AND EAX,0xF
        MOV DWORD PTR CPUVendor[0x10],EAX
        SHR EBX,0x4
        MOV EAX,EBX
        AND EAX,0xF
        MOV DWORD PTR CPUVendor[0x14],EAX

        LEA EDI,CPUVendor
        ADD EDI,0x18
        MOV ECX,0x6
        CALL PlaceBytes

        SHR EDX,1
        MOV ECX,0x3
        CALL PlaceBytes
        SHR EDX,0x2
        MOV ECX,0x2
        CALL PlaceBytes
        SHR EDX,1
        MOV ECX,0x1
        CALL PlaceBytes
        SHR EDX,0x7
        MOV ECX,0x1
        CALL PlaceBytes

        MOV DWORD PTR KeepAddress[0],EDI

        MOV ECX,0x0C
        LEA ESI,CPUVendor
        LEA EDI,AMDCPU
        REPE CMPS BYTE PTR [EDI],BYTE PTR [ESI]
        JNZ SkippAMDStuff

        MOV EAX,0x80000000
        CPUID
        TEST AL,AL
        JE SkippAMDStuff

        MOV EDI,DWORD PTR KeepAddress[0]

        MOV EAX,0x80000001
        CPUID
        MOV EAX,EDX
        SHR EAX,0xB
        AND AL,0x1
        MOV BYTE PTR DS:[EDI],AL
        MOV EAX,EDX
        SHR EAX,0x10
        AND AL,0x1
        MOV BYTE PTR DS:[EDI+0x1],AL
        MOV EAX,EDX
        SHR EAX,0x1F
        AND AL,0x1
        MOV BYTE PTR DS:[EDI+0x2],AL
        LEA EDI,CPUVendorP2
        MOV EAX,0x0
        MOV DWORD PTR DS:[EDI],EAX
        MOV EAX,0x80000000
        CPUID
        CMP EAX,0x80000004
        JL SHORT JustCopyString
        MOV EAX,0x80000002
        CPUID
        MOV DWORD PTR DS:[EDI],    EAX
        MOV DWORD PTR DS:[EDI+0x4],EBX
        MOV DWORD PTR DS:[EDI+0x8],ECX
        MOV DWORD PTR DS:[EDI+0xC],EDX
        ADD EDI,0x10
        MOV EAX,0x80000003
        CPUID
        MOV DWORD PTR DS:[EDI],    EAX
        MOV DWORD PTR DS:[EDI+0x4],EBX
        MOV DWORD PTR DS:[EDI+0x8],ECX
        MOV DWORD PTR DS:[EDI+0xC],EDX
        ADD EDI,0x10
        MOV EAX,0x80000004
        CPUID
        MOV DWORD PTR DS:[EDI],    EAX
        MOV DWORD PTR DS:[EDI+0x4],EBX
        MOV DWORD PTR DS:[EDI+0x8],ECX
        MOV DWORD PTR DS:[EDI+0xC],EDX

    JustCopyString:
        LEA ESI,CPUVendorP2
        LEA EDI,CPUVendor
        ADD EDI,0x28
        MOV ECX,0x30
        REP MOVS BYTE PTR ES:[EDI],BYTE PTR DS:[ESI]

    SkippAMDStuff:
        JMP End

    PlaceBytes:
        MOV AL,DL
        AND AL,0x1
        MOV BYTE PTR DS:[EDI],AL
        SHR EDX,1
        INC EDI
        LOOP PlaceBytes
        RETN

    End:
    }

    spdlog::debug("GetCPUVendorID 完成，Vendor='{:.12s}'", CPUVendor);
}

/////////////////////////////////////////////////////////////////////////////
// CRC32

void crc_generate_table(unsigned int* table)
{
    const unsigned int polynomial = 0xEDB88320u;
    for (int i = 0; i < 256; i++) {              // [M03]
        unsigned int c = static_cast<unsigned int>(i);
        for (int j = 0; j < 8; j++) {
            if (c & 1) c = polynomial ^ (c >> 1);
            else       c >>= 1;
        }
        table[i] = c;
    }
}

unsigned int crc_update(unsigned int* table, unsigned int initial,
                        const void* buf, size_t len)
{
    unsigned int c = initial ^ 0xFFFFFFFFu;
    const unsigned char* u = static_cast<const unsigned char*>(buf);
    for (size_t i = 0; i < len; ++i)
        c = table[(c ^ u[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/////////////////////////////////////////////////////////////////////////////
// IsWow64 / GetRegistryKeyValue

static bool IsWow64()
{
    BOOL bIsWow64 = FALSE;
    typedef BOOL(APIENTRY* LPFN_ISWOW64PROCESS)(HANDLE, PBOOL);
    HMODULE module = GetModuleHandle(_T("kernel32"));
    auto fn = reinterpret_cast<LPFN_ISWOW64PROCESS>(
        GetProcAddress(module, "IsWow64Process"));
    if (fn && !fn(GetCurrentProcess(), &bIsWow64))
        throw std::exception("IsWow64Process 调用失败");
    return bIsWow64 != FALSE;
}

#ifndef KEY_WOW64_64KEY
#  define KEY_WOW64_64KEY 0x0100
#endif

static byte* GetRegistryKeyValue(const char* RegKey, const char* pPIDName)
{
    HKEY  Registry;
    DWORD regType = 0;
    DWORD regSize = 0;

    DWORD ulOptions = KEY_QUERY_VALUE;
    if (IsWow64()) ulOptions |= KEY_WOW64_64KEY;

    long ReturnStatus = RegOpenKeyExA(HKEY_LOCAL_MACHINE, RegKey,
                                      0, ulOptions, &Registry);
    if (ReturnStatus != ERROR_SUCCESS) {
        spdlog::warn("GetRegistryKeyValue: RegOpenKeyExA('{}') 失败，code={}",
                     RegKey, ReturnStatus);
        RegCloseKey(Registry);
        return nullptr;
    }

    RegQueryValueExA(Registry, pPIDName, NULL, &regType, 0, &regSize);

    // [M02 保留风格] 分配 regSize+1，防止越界
    byte* pPID = new byte[regSize + 1];
    pPID[regSize] = '\0';
    RegQueryValueExA(Registry, pPIDName, NULL, NULL,
                     reinterpret_cast<unsigned char*>(pPID), &regSize);
    RegCloseKey(Registry);

    if (regSize > 0 && (pPID[regSize-1] > 127 || pPID[regSize-1] < 32))
        pPID[regSize-1] = '\0';

    if (regSize > 1) {
        spdlog::debug("GetRegistryKeyValue('{}') 成功，读取 {} 字节", pPIDName, regSize);
        return pPID;
    } else {
        spdlog::warn("GetRegistryKeyValue('{}') 返回大小异常={}", pPIDName, regSize);
        delete[] pPID;
        return nullptr;
    }
}

/////////////////////////////////////////////////////////////////////////////
// GetWindowSerial — 读取 Windows 产品密钥
// [M02] std::auto_ptr → std::unique_ptr<byte[]> / std::unique_ptr<char[]>
// [M03] for 循环变量作用域修正（1 处）

char WindowsSerial[29 + 1];

void GetWindowSerial()
{
    spdlog::debug("GetWindowSerial: 读取注册表 DigitalProductId");

    // [M02] unique_ptr 自动管理 GetRegistryKeyValue 分配的堆内存
    std::unique_ptr<byte[]> spPID(
        GetRegistryKeyValue(
            "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            "DigitalProductId"));
    unsigned char* digitalProductId = spPID.get();

    if (!digitalProductId) {
        spdlog::warn("GetWindowSerial: 无法读取 DigitalProductId");
        return;
    }

    const int keyStartIndex   = 52;
    const int keyEndIndex     = keyStartIndex + 15;
    const int decodeLength    = 29;
    const int decodeStringLen = 15;
    const char digits[]       = {
        'B','C','D','F','G','H','J','K','M','P','Q','R',
        'T','V','W','X','Y','2','3','4','6','7','8','9',
    };

    // [M02] unique_ptr<char[]> 自动释放
    std::unique_ptr<char[]> spDecoded(new char[decodeLength + 1]);
    char* pDecodedChars = spDecoded.get();
    memset(pDecodedChars, 0, decodeLength + 1);

    byte hexPid[keyEndIndex - keyStartIndex + 1];
    for (int i = keyStartIndex; i <= keyEndIndex; i++)   // [M03]
        hexPid[i - keyStartIndex] = digitalProductId[i];

    for (int i = decodeLength - 1; i >= 0; i--) {        // [M03]
        if ((i + 1) % 6 == 0) {
            pDecodedChars[i] = '-';
        } else {
            int digitMapIndex = 0;
            for (int j = decodeStringLen - 1; j >= 0; j--) {
                int byteValue     = (digitMapIndex << 8) | hexPid[j];
                hexPid[j]         = static_cast<byte>(byteValue / 24);
                digitMapIndex     = byteValue % 24;
                pDecodedChars[i]  = digits[digitMapIndex];
            }
        }
    }

    strncpy_s(WindowsSerial, sizeof(WindowsSerial),      // [M06]
              pDecodedChars, _TRUNCATE);
    spdlog::info("GetWindowSerial 成功，序列号='{}'", WindowsSerial);
}

/////////////////////////////////////////////////////////////////////////////
// SMBIOS 结构定义

typedef UINT(WINAPI* PGETSYSTEMFIRMWARETABLE)(DWORD, DWORD, PVOID, DWORD);

struct SMBIOSHEADER {
    uint8_t  type;
    uint8_t  length;
    uint16_t handle;
};

struct SMBIOSData {
    uint8_t  Used20CallingMethod;
    uint8_t  SMBIOSMajorVersion;
    uint8_t  SMBIOSMinorVersion;
    uint8_t  DmiRevision;
    uint32_t Length;
    uint8_t  SMBIOSTableData[1];
};

struct SYSTEMINFORMATION {
    SMBIOSHEADER Header;
    uint8_t Manufacturer;
    uint8_t ProductName;
    uint8_t Version;
    uint8_t SerialNumber;
    uint8_t UUID[16];
    uint8_t WakeUpType;
    uint8_t SKUNumber;
    uint8_t Family;
};

static SYSTEMINFORMATION* find_system_information(SMBIOSData* bios_data)
{
    uint8_t* data = bios_data->SMBIOSTableData;
    while (data < bios_data->SMBIOSTableData + bios_data->Length) {
        SMBIOSHEADER* header = reinterpret_cast<SMBIOSHEADER*>(data);
        if (header->length < 4) break;
        if (header->type == 0x01 && header->length >= 0x19)
            return reinterpret_cast<SYSTEMINFORMATION*>(header);
        uint8_t* next = data + header->length;
        while (next < bios_data->SMBIOSTableData + bios_data->Length &&
               (next[0] != 0 || next[1] != 0))
            next++;
        next += 2;
        data = next;
    }
    return nullptr;
}

static const char* get_string_by_index(const char* str, int index,
                                        const char* null_text = "")
{
    if (index == 0 || *str == '\0') return null_text;
    while (--index) str += strlen(str) + 1;
    return str;
}

/////////////////////////////////////////////////////////////////////////////
// GetMotherboard — 通过 SMBIOS 获取主板信息
// [M04] strcat 逐次拼接 → std::string 安全拼接
// [M08] malloc 裸指针 → std::unique_ptr<BYTE[]>

char Motherboard[MAX_PATH + 3];

void GetMotherboard()
{
    Motherboard[0] = '\0';
    spdlog::debug("GetMotherboard: 查询 SMBIOS 固件表");

    auto pGetSFT = reinterpret_cast<PGETSYSTEMFIRMWARETABLE>(
        GetProcAddress(GetModuleHandle("kernel32.dll"), "GetSystemFirmwareTable"));
    if (!pGetSFT) {
        spdlog::warn("GetMotherboard: GetSystemFirmwareTable 不可用（需 XP SP2+）");
        return;
    }

    DWORD dwSize = pGetSFT('RSMB', 0, NULL, 0);
    if (!dwSize) {
        spdlog::warn("GetMotherboard: SMBIOS 大小查询返回 0");
        return;
    }

    // [M08] unique_ptr 替代 malloc + free
    std::unique_ptr<BYTE[]> pSmBiosBuf(new BYTE[dwSize]);
    pGetSFT('RSMB', 0, pSmBiosBuf.get(), dwSize);

    auto* bios_data = reinterpret_cast<SMBIOSData*>(pSmBiosBuf.get());
    SYSTEMINFORMATION* sysinfo = find_system_information(bios_data);
    if (!sysinfo) {
        spdlog::warn("GetMotherboard: 未找到 SMBIOS Type 1（System Information）结构");
        return;
    }

    const char* str = reinterpret_cast<const char*>(sysinfo) + sysinfo->Header.length;

    // [M04] std::string 拼接，彻底避免 strcat 溢出
    std::string sb;
    sb.reserve(512);
    sb += get_string_by_index(str, sysinfo->Manufacturer);
    sb += get_string_by_index(str, sysinfo->ProductName);
    sb += get_string_by_index(str, sysinfo->Version);
    sb += get_string_by_index(str, sysinfo->SerialNumber);

    // UUID（SMBIOS v2.1+）
    if (sysinfo->Header.length > 0x08) {
        char uuid[50];
        sprintf_s(uuid, sizeof(uuid),
            "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
            sysinfo->UUID[0],  sysinfo->UUID[1],  sysinfo->UUID[2],  sysinfo->UUID[3],
            sysinfo->UUID[4],  sysinfo->UUID[5],  sysinfo->UUID[6],  sysinfo->UUID[7],
            sysinfo->UUID[8],  sysinfo->UUID[9],  sysinfo->UUID[10], sysinfo->UUID[11],
            sysinfo->UUID[12], sysinfo->UUID[13], sysinfo->UUID[14], sysinfo->UUID[15]);
        sb += uuid;
    }

    // SKU + Family（SMBIOS v2.4+）
    if (sysinfo->Header.length > 0x19) {
        sb += get_string_by_index(str, sysinfo->SKUNumber);
        sb += get_string_by_index(str, sysinfo->Family);
    }

    strncpy_s(Motherboard, sizeof(Motherboard), sb.c_str(), _TRUNCATE);
    spdlog::info("GetMotherboard 成功，原始长度={}，存储长度={}",
                 sb.size(), strlen(Motherboard));
    spdlog::debug("Motherboard='{}'", Motherboard);
}

/////////////////////////////////////////////////////////////////////////////
// RC4 初始化与流加密

void CEnigmaHardwareIDDlg::InitializeRC4Key(unsigned char* pKey, unsigned int lenKey)
{
    spdlog::debug("InitializeRC4Key: 初始化 S-Box，密钥长度={}", lenKey);
    b = 0;
    for (a = 0; a < 256; a++)
        m_sBox[a] = a;
    for (a = 0; a < 256; a++) {
        b    = (b + m_sBox[a] + pKey[a % lenKey]) % 256;
        swap = static_cast<unsigned char>(m_sBox[a]);
        m_sBox[a] = m_sBox[b];
        m_sBox[b] = swap;
    }
}

void CEnigmaHardwareIDDlg::RC4(unsigned char pData[], unsigned long lenData)
{
    int sBox[256];
    memcpy(sBox, m_sBox, 256 * sizeof(int));
    int i = 0, j = 0;
    for (unsigned long offset = 0; offset < lenData; offset++) {
        i = (i + 1) % 256;
        j = (j + sBox[i]) % 256;
        int tmp  = sBox[i];
        sBox[i]  = sBox[j];
        sBox[j]  = tmp;
        pData[offset] ^= static_cast<unsigned char>(sBox[(sBox[i] + sBox[j]) % 256]);
    }
}

/////////////////////////////////////////////////////////////////////////////
// OnGetbutton — 采集所有硬件指纹并计算 CRC32

#define INFO_BUFFER_SIZE 32767
static WCHAR g_ComputerName[INFO_BUFFER_SIZE];
static WCHAR g_UserName[INFO_BUFFER_SIZE];
unsigned int ValuesTable[8];

void CEnigmaHardwareIDDlg::OnGetbutton()
{
    spdlog::info("=== 开始采集硬件指纹 ===");

    unsigned int table[256];
    crc_generate_table(table);
    unsigned int CRC = 0;
    char CRCstr[12];

    // ── 计算机名 ─────────────────────────────────────────────
    DWORD bufCharCount = INFO_BUFFER_SIZE;
    g_ComputerName[0] = 0;
    if (IsDlgButtonChecked(IDC_COMPNCB) &&
        GetComputerNameW(g_ComputerName, &bufCharCount)) {
        SetDlgItemTextW(m_hWnd, IDC_EDIT1, g_ComputerName);
        CRC = crc_update(table, 0, g_ComputerName, wcslen(g_ComputerName) * 2);
        ValuesTable[0] = CRC;
        sprintf_s(CRCstr, sizeof(CRCstr), "%08X", CRC);         // [M06]
        SetDlgItemText(IDC_EDIT2, CRCstr);
        spdlog::debug("ComputerName  CRC32={}", CRCstr);
    } else {
        SetDlgItemText(IDC_EDIT1, ""); SetDlgItemText(IDC_EDIT2, "");
        ValuesTable[0] = 0;
    }

    // ── CPU 信息 ─────────────────────────────────────────────
    CPUVendor[0] = '\0';
    if (IsDlgButtonChecked(IDC_CPUTCB)) {
        GetCPUVendorID();
        if (CPUVendor[0]) {
            char CPUVendorInfo[4000];
            strcpy_s(CPUVendorInfo, sizeof(CPUVendorInfo), CPUVendor);  // [M06]
            strncat_s(CPUVendorInfo, sizeof(CPUVendorInfo), "..", _TRUNCATE);
            strncat_s(CPUVendorInfo, sizeof(CPUVendorInfo), CPUVendorP2, _TRUNCATE);
            SetDlgItemText(IDC_EDIT3, CPUVendorInfo);
            CRC = crc_update(table, 0, CPUVendor, 0x58);
            ValuesTable[1] = CRC;
            sprintf_s(CRCstr, sizeof(CRCstr), "%08X", CRC);
            SetDlgItemText(IDC_EDIT4, CRCstr);
            spdlog::debug("CPU           CRC32={}", CRCstr);
        } else {
            SetDlgItemText(IDC_EDIT3, ""); SetDlgItemText(IDC_EDIT4, "");
            ValuesTable[1] = 0;
        }
    } else {
        SetDlgItemText(IDC_EDIT3, ""); SetDlgItemText(IDC_EDIT4, "");
        ValuesTable[1] = 0;
    }

    // ── 系统卷序列号 ─────────────────────────────────────────
    char VolumeSN[20];
    DWORD VolumeSerialNumber = 0;
    char CPath[20];
    char winpath[MAX_PATH + 3];
    GetWindowsDirectoryA(winpath, sizeof(winpath));
    CPath[0] = winpath[0]; CPath[1] = winpath[1];
    CPath[2] = winpath[2]; CPath[3] = '\0';

    if (IsDlgButtonChecked(IDC_SVSNCB) &&
        GetVolumeInformationA(CPath, NULL, NULL, &VolumeSerialNumber,
                              NULL, NULL, NULL, NULL)) {
        wsprintf(VolumeSN, "%08X", VolumeSerialNumber);
        SetDlgItemTextA(IDC_EDIT5, VolumeSN);
        ValuesTable[2] = VolumeSerialNumber;
        spdlog::debug("VolumeSerial  值={}", VolumeSN);
    } else {
        SetDlgItemText(IDC_EDIT5, ""); ValuesTable[2] = 0;
    }

    // ── 主板信息 ─────────────────────────────────────────────
    if (IsDlgButtonChecked(IDC_MOTHERCB)) {
        if (IsDlgButtonChecked(IDC_0x200EMPTY)) {
            char emtychars[0x200];
            memset(emtychars, 0, sizeof(emtychars));              // [M07]
            SetDlgItemText(IDC_EDIT6, "");
            CRC = crc_update(table, 0, emtychars, 0x200);
            ValuesTable[3] = CRC;
            sprintf_s(CRCstr, sizeof(CRCstr), "%08X", CRC);
            SetDlgItemText(IDC_EDIT7, CRCstr);
            spdlog::debug("Motherboard   CRC32={} (空填充模式)", CRCstr);
        } else {
            GetMotherboard();
            if (Motherboard[0]) {
                SetDlgItemText(IDC_EDIT6, Motherboard);
                CRC = crc_update(table, 0, Motherboard, strlen(Motherboard));
                ValuesTable[3] = CRC;
                sprintf_s(CRCstr, sizeof(CRCstr), "%08X", CRC);
                SetDlgItemText(IDC_EDIT7, CRCstr);
                spdlog::debug("Motherboard   CRC32={}", CRCstr);
            } else {
                SetDlgItemText(IDC_EDIT6, ""); SetDlgItemText(IDC_EDIT7, "");
                ValuesTable[3] = 0;
            }
        }
    } else {
        SetDlgItemText(IDC_EDIT6, ""); SetDlgItemText(IDC_EDIT7, "");
        ValuesTable[3] = 0;
    }

    // ── 卷名 ─────────────────────────────────────────────────
    WCHAR  CPathU[8];
    WCHAR  winpathU[MAX_PATH + 3];
    GetWindowsDirectoryW(winpathU, MAX_PATH);
    CPathU[0] = winpathU[0]; CPathU[1] = winpathU[1];
    CPathU[2] = winpathU[2]; CPathU[3] = 0;
    WCHAR szVolumeName[MAX_PATH];
    if (IsDlgButtonChecked(IDC_SVNCB) &&
        GetVolumeInformationW(CPathU, szVolumeName, MAX_PATH,
                              NULL, NULL, NULL, NULL, 0)) {
        SetDlgItemTextW(m_hWnd, IDC_EDIT8, szVolumeName);
        CRC = crc_update(table, 0, szVolumeName, wcslen(szVolumeName));
        ValuesTable[4] = CRC;
        sprintf_s(CRCstr, sizeof(CRCstr), "%08X", CRC);
        SetDlgItemText(IDC_EDIT9, CRCstr);
        spdlog::debug("VolumeName    CRC32={}", CRCstr);
    } else {
        SetDlgItemText(IDC_EDIT8, ""); SetDlgItemText(IDC_EDIT9, "");
        ValuesTable[4] = 0;
    }

    // ── Windows 序列号 ───────────────────────────────────────
    if (IsDlgButtonChecked(IDC_WSNCB)) {
        GetWindowSerial();
        if (WindowsSerial[0]) {
            SetDlgItemText(IDC_EDIT10, WindowsSerial);
            CRC = crc_update(table, 0, WindowsSerial, strlen(WindowsSerial));
            ValuesTable[5] = CRC;
            sprintf_s(CRCstr, sizeof(CRCstr), "%08X", CRC);
            SetDlgItemText(IDC_EDIT11, CRCstr);
            spdlog::debug("WinSerial     CRC32={}", CRCstr);
        } else {
            SetDlgItemText(IDC_EDIT10, ""); SetDlgItemText(IDC_EDIT11, "");
            ValuesTable[5] = 0;
        }
    } else {
        SetDlgItemText(IDC_EDIT10, ""); SetDlgItemText(IDC_EDIT11, "");
        ValuesTable[5] = 0;
    }

    // ── 硬盘序列号 ───────────────────────────────────────────
    HDDStr[0] = '\0';
    if (IsDlgButtonChecked(IDC_HDDSNCB)) {
        GetHDDString();
        if (HDDStr[0]) {
            SetDlgItemText(IDC_EDIT12, HDDStr);
            CRC = crc_update(table, 0, HDDStr, strlen(HDDStr));
            ValuesTable[6] = CRC;
            sprintf_s(CRCstr, sizeof(CRCstr), "%08X", CRC);
            SetDlgItemText(IDC_EDIT13, CRCstr);
            spdlog::debug("HDD           CRC32={}", CRCstr);
        } else {
            SetDlgItemText(IDC_EDIT12, ""); SetDlgItemText(IDC_EDIT13, "");
            ValuesTable[6] = 0;
        }
    } else {
        SetDlgItemText(IDC_EDIT12, ""); SetDlgItemText(IDC_EDIT13, "");
        ValuesTable[6] = 0;
    }

    // ── 用户名 ───────────────────────────────────────────────
    DWORD bufCharCount2 = INFO_BUFFER_SIZE;
    g_UserName[0] = 0;
    if (IsDlgButtonChecked(IDC_USERNCB) &&
        GetUserNameW(g_UserName, &bufCharCount2)) {
        CharLowerBuffW(g_UserName, static_cast<DWORD>(wcslen(g_UserName)));
        SetDlgItemTextW(m_hWnd, IDC_EDIT14, g_UserName);
        CRC = crc_update(table, 0, g_UserName, (wcslen(g_UserName) + 1) * 2);
        ValuesTable[7] = CRC;
        sprintf_s(CRCstr, sizeof(CRCstr), "%08X", CRC);
        SetDlgItemText(IDC_EDIT15, CRCstr);
        spdlog::debug("UserName      CRC32={}", CRCstr);
    } else {
        SetDlgItemText(IDC_EDIT14, ""); SetDlgItemText(IDC_EDIT15, "");
        ValuesTable[7] = 0;
    }

    spdlog::info("=== 硬件指纹采集完成，ValuesTable[0..7] 已填充 ===");
}

/////////////////////////////////////////////////////////////////////////////
// OnGettrialkey — 填入 Demo RSA Key

void CEnigmaHardwareIDDlg::OnGettrialkey()
{
    spdlog::info("OnGettrialkey: 加载 Demo RSA Key（来自 config.h）");
    SetDlgItemText(IDC_RSAKEY, ENIGMA_DEMO_RSA_KEY);
}

/////////////////////////////////////////////////////////////////////////////
// 字符串编解码辅助

static const char AllowedChars[] = "ABCDEF1234567890- \r\n";

static char CharToIndex(char tch)
{
    int len2 = static_cast<int>(strlen(AllowedChars));
    for (int j = 0; j < len2; j++)
        if (tch == AllowedChars[j]) return static_cast<char>(j);
    return -1;
}

// [M03] for 循环变量 i 已统一在循环头声明
int CEnigmaHardwareIDDlg::DecodeString(char* ToDecode, char* destination)
{
    char charindexP = 0;
    bool WasPrevious = false;
    int  dindx = 0;
    int  len   = static_cast<int>(strlen(ToDecode));

    for (int i = 0; i < len; i++) {                      // [M03]
        char char1index = CharToIndex(ToDecode[i]);
        if (char1index < 0) {
            SetDlgItemText(IDC_RC4Key, "Invalid char!");
            spdlog::warn("DecodeString: 发现非法字符 '{}' at pos {}", ToDecode[i], i);
            return 0;
        }
        if (char1index > 0x0F) continue;
        if (!WasPrevious) {
            charindexP  = char1index;
            WasPrevious = true;
        } else {
            destination[dindx++] = static_cast<char>(charindexP * 16 + char1index);
            WasPrevious = false;
        }
    }
    return dindx;
}

int CEnigmaHardwareIDDlg::EncodeToString(char* ToEncode, int enlen, char* destination)
{
    int addSep = 0;
    if (IsDlgButtonChecked(IDC_INSERTSEP)) {
        if      ((enlen * 2) % 6 == 0) addSep = 6;
        else if ((enlen * 2) % 5 == 0) addSep = 5;
    }
    int insertCount = 0;
    for (int i = 0; i < enlen; i++) {                    // [M03]
        if (addSep != 0 && i != 0 && (2 * i) % addSep == 0)
            destination[2 * i + insertCount++] = '-';
        destination[2 * i + insertCount]     = AllowedChars[(ToEncode[i] & 0xF0) >> 4];
        if (addSep != 0 && i != 0 && (2 * i + 1) % addSep == 0)
            destination[2 * i + 1 + insertCount++] = '-';
        destination[2 * i + 1 + insertCount] = AllowedChars[ToEncode[i] & 0xF];
    }
    destination[2 * enlen + insertCount] = '\0';
    return 0;
}

/////////////////////////////////////////////////////////////////////////////
// ExtractRC4EncryptionKeyFromRsa
// [M03] for 循环变量作用域修正（1 处）
// [M06] sprintf → sprintf_s

void CEnigmaHardwareIDDlg::ExtractRC4EncryptionKeyFromRsa()
{
    CWnd* hWndRSAKey = GetDlgItem(IDC_RSAKEY);
    char RSAKey[MAX_PATH];
    ::GetWindowTextA(hWndRSAKey->m_hWnd, RSAKey, MAX_PATH);
    if (RSAKey[0] == '\0') return;

    char KeyLenStr[5];
    memcpy(KeyLenStr, RSAKey, 3);
    KeyLenStr[3] = '\0';

    std::istringstream converter(KeyLenStr);
    unsigned int KeyLen;
    converter >> std::hex >> KeyLen;
    spdlog::debug("ExtractRC4: RSA Key 数据长度={}", KeyLen);

    memcpy(RSAKey, RSAKey + 3, KeyLen);
    RSAKey[KeyLen] = '\0';

    char keydest[0x40];
    int declen = DecodeString(RSAKey, keydest);
    if (declen <= 0) {
        SetDlgItemText(IDC_RC4Key, "Failed to decode!");
        spdlog::error("ExtractRC4: DecodeString 失败");
        return;
    }

    InitializeRC4Key(reinterpret_cast<unsigned char*>(keydest),
                     static_cast<unsigned int>(declen));

    char hexKey[256 * 2 + 1];
    for (int i = 0; i < 256; i++)                         // [M03]
        sprintf_s(hexKey + i * 2, 3, "%.2X",              // [M06]
                  static_cast<unsigned char>(m_sBox[i]));
    hexKey[256 * 2] = '\0';
    SetDlgItemText(IDC_RC4Key, hexKey);
    spdlog::info("RC4 密钥提取成功（前 16 字节: {:.16s}...）", hexKey);
}

void CEnigmaHardwareIDDlg::OnExtractrc4keyfromrsa()
{
    ExtractRC4EncryptionKeyFromRsa();
}

/////////////////////////////////////////////////////////////////////////////
// OnDecodehwid — 解码 HWID
// [M03] for 循环变量作用域修正（2 处）
// [M09] 8 段重复组件解析块 → C++11 Lambda

void CEnigmaHardwareIDDlg::OnDecodehwid()
{
    spdlog::info("=== 开始解码 HWID ===");

    CWnd* hWndHID = GetDlgItem(IDC_HARDWAREID);
    char HARDWAREID[MAX_PATH];
    ::GetWindowTextA(hWndHID->m_hWnd, HARDWAREID, MAX_PATH);
    if (HARDWAREID[0] == '\0') return;

    CWnd* hWndRCKey = GetDlgItem(IDC_RC4Key);
    char RC4KeyStr[256 * 2 + 1];
    ::GetWindowTextA(hWndRCKey->m_hWnd, RC4KeyStr, sizeof(RC4KeyStr));
    if (RC4KeyStr[0] == '\0') {
        SetDlgItemText(IDC_INFO, "RC4 key can't be empty!");
        spdlog::warn("OnDecodehwid: RC4 密钥为空");
        return;
    }

    int RC4Len = static_cast<int>(strlen(RC4KeyStr));
    for (int i = 0; i < RC4Len; i += 2)                  // [M03]
        m_sBox[i / 2] = hextobyte(RC4KeyStr[i]) * 16 + hextobyte(RC4KeyStr[i + 1]);

    // 清空所有显示字段
    const int clearIDs[] = { IDC_EDIT1, IDC_EDIT3, IDC_EDIT5, IDC_EDIT6,
                              IDC_EDIT8, IDC_EDIT10, IDC_EDIT12, IDC_EDIT14 };
    for (int id : clearIDs) SetDlgItemText(id, "");

    char decodestr[MAX_PATH];
    int  declen1 = DecodeString(HARDWAREID, decodestr);

    unsigned short CRCVal =
        (static_cast<unsigned char>(decodestr[1]) << 8) |
         static_cast<unsigned char>(decodestr[0]);

    RC4(reinterpret_cast<unsigned char*>(decodestr) + 2,
        static_cast<unsigned long>(declen1 - 2));

    unsigned int table[256];
    crc_generate_table(table);
    unsigned int CRC = crc_update(table, 0, decodestr + 2, declen1 - 2);

    if ((CRC & 0x0FFFF) != CRCVal) {
        SetDlgItemText(IDC_INFO, "CRC failed!");
        spdlog::error("OnDecodehwid: CRC 校验失败，期望={:04X}，实际={:04X}",
                      CRCVal, CRC & 0x0FFFF);
        return;
    }
    spdlog::info("OnDecodehwid: CRC {:04X} 校验通过", CRCVal);

    char decdata[256 * 2 + 1];
    for (int i = 0; i < declen1; i++)                     // [M03]
        sprintf_s(decdata + i * 2, 3, "%.2X",             // [M06]
                  static_cast<unsigned char>(decodestr[i]));
    decdata[declen1 * 2] = '\0';
    SetDlgItemText(IDC_EDIT16, decdata);

    // [M09] Lambda：统一处理各组件位标志的解析与 UI 更新
    int srcindex = 0;
    auto readWord = [&](int editId, int checkboxId, uint8_t bit) {
        if (decodestr[2] & bit) {
            char buf[5];
            // 字节序：低字节在前（little-endian）
            sprintf_s(buf,     3, "%.2X",
                      static_cast<unsigned char>(decodestr[2 + 2 + srcindex * 2 + 1]));
            sprintf_s(buf + 2, 3, "%.2X",
                      static_cast<unsigned char>(decodestr[2 + 2 + srcindex * 2]));
            buf[4] = '\0';
            SetDlgItemText(editId, buf);
            srcindex++;
            if (IsDlgButtonChecked(IDC_CHECKUSED))
                CheckDlgButton(checkboxId, BST_CHECKED);
        } else if (IsDlgButtonChecked(IDC_CHECKUSED)) {
            CheckDlgButton(checkboxId, BST_UNCHECKED);
        }
    };

    readWord(IDC_EDIT5,  IDC_SVSNCB,  0x01);  // 系统卷序列号
    readWord(IDC_EDIT9,  IDC_SVNCB,   0x02);  // 卷名
    readWord(IDC_EDIT4,  IDC_CPUTCB,  0x04);  // CPU 类型
    readWord(IDC_EDIT2,  IDC_COMPNCB, 0x08);  // 计算机名
    readWord(IDC_EDIT7,  IDC_MOTHERCB,0x10);  // 主板
    readWord(IDC_EDIT11, IDC_WSNCB,   0x20);  // Windows 序列号
    readWord(IDC_EDIT13, IDC_HDDSNCB, 0x40);  // 硬盘序列号
    readWord(IDC_EDIT15, IDC_USERNCB, 0x80);  // 用户名

    spdlog::info("=== HWID 解码完成，共 {} 个有效组件 ===", srcindex);
}

/////////////////////////////////////////////////////////////////////////////
// GetWordFromString

static bool GetWordFromString(const char* str, char* data, int datapos)
{
    if (str[0] == '\0') return false;
    int len = static_cast<int>(strlen(str));
    int idx = (len > 4) ? (len - 4) : 0;
    std::istringstream conv(str + idx);
    unsigned int intData;
    conv >> std::hex >> intData;
    data[datapos]     = static_cast<char>( intData        & 0xFF);
    data[datapos + 1] = static_cast<char>((intData >> 8)  & 0xFF);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// OnGenhwid — 生成 HWID
// [M03] for 循环变量作用域修正（1 处）
// [M09] 8 段重复组件打包块 → C++11 Lambda

void CEnigmaHardwareIDDlg::OnGenhwid()
{
    spdlog::info("=== 开始生成 HWID ===");

    char data[2 + 2 + 2 * 8];
    data[2] = data[3] = '\0';
    char varstring[MAX_PATH];
    CWnd* hWndVal = nullptr;
    int elementsCount = 0;

    // [M09] Lambda：统一打包各组件的 CRC-16 值
    auto packComponent = [&](int checkboxId, int editId, uint8_t bit) {
        varstring[0] = '\0';
        if (IsDlgButtonChecked(checkboxId)) {
            hWndVal = GetDlgItem(editId);
            ::GetWindowTextA(hWndVal->m_hWnd, varstring, MAX_PATH);
            if (GetWordFromString(varstring, data, 4 + elementsCount * 2)) {
                elementsCount++;
                data[2] |= static_cast<char>(bit);
            }
        }
    };

    packComponent(IDC_SVSNCB,  IDC_EDIT5,  0x01);  // 系统卷序列号
    packComponent(IDC_SVNCB,   IDC_EDIT9,  0x02);  // 卷名
    packComponent(IDC_CPUTCB,  IDC_EDIT4,  0x04);  // CPU 类型
    packComponent(IDC_COMPNCB, IDC_EDIT2,  0x08);  // 计算机名
    packComponent(IDC_MOTHERCB,IDC_EDIT7,  0x10);  // 主板
    packComponent(IDC_WSNCB,   IDC_EDIT11, 0x20);  // Windows 序列号
    packComponent(IDC_HDDSNCB, IDC_EDIT13, 0x40);  // 硬盘序列号
    packComponent(IDC_USERNCB, IDC_EDIT15, 0x80);  // 用户名

    spdlog::debug("OnGenhwid: 打包 {} 个组件，位掩码=0x{:02X}",
                  elementsCount,
                  static_cast<unsigned char>(data[2]));

    // CRC-16（取 CRC32 低 16 位）
    unsigned int table[256];
    crc_generate_table(table);
    unsigned int CRC = crc_update(table, 0, data + 2, 2 + elementsCount * 2);
    CRC &= 0x0FFFF;
    data[0] = static_cast<char>( CRC        & 0xFF);
    data[1] = static_cast<char>((CRC >> 8)  & 0xFF);

    int datalen = 4 + elementsCount * 2;

    // 显示原始字节序列
    char decdata[256 * 2 + 1];
    for (int i = 0; i < datalen; i++)                     // [M03]
        sprintf_s(decdata + i * 2, 3, "%.2X",             // [M06]
                  static_cast<unsigned char>(data[i]));
    decdata[datalen * 2] = '\0';
    SetDlgItemText(IDC_EDIT16, decdata);

    // 读取 RC4 密钥
    CWnd* hWndRCKey = GetDlgItem(IDC_RC4Key);
    char RC4KeyStr[256 * 2 + 1];
    ::GetWindowTextA(hWndRCKey->m_hWnd, RC4KeyStr, sizeof(RC4KeyStr));
    if (RC4KeyStr[0] == '\0') {
        SetDlgItemText(IDC_INFO, "RC4 key can't be empty!");
        spdlog::warn("OnGenhwid: RC4 密钥为空");
        return;
    }

    int RC4Len = static_cast<int>(strlen(RC4KeyStr));
    for (int i = 0; i < RC4Len; i += 2)                  // [M03]
        m_sBox[i / 2] = hextobyte(RC4KeyStr[i]) * 16 + hextobyte(RC4KeyStr[i + 1]);

    // RC4 加密（跳过前 2 字节 CRC）
    RC4(reinterpret_cast<unsigned char*>(data) + 2,
        static_cast<unsigned long>(datalen - 2));

    // 编码为可打印字符串并显示
    char HWID[256];
    HWID[0] = '\0';
    EncodeToString(data, datalen, HWID);
    if (HWID[0]) SetDlgItemText(IDC_HARDWAREID, HWID);

    spdlog::info("=== HWID 生成完成: '{}' ===", HWID);
}
