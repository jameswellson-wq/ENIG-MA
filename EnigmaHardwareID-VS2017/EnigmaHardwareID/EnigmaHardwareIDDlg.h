// EnigmaHardwareIDDlg.h — 对话框头文件（现代化版，VS2017+）
// ============================================================
// 变更：
//   [M] 移除 VC6 ClassWizard 标记块（AFX_MSG / AFX_DATA 等）
//   [M] #pragma once 替代旧式 include guard
//   [M] NULL → nullptr
// ============================================================
#pragma once

/////////////////////////////////////////////////////////////////////////////
// CEnigmaHardwareIDDlg — 硬件指纹对话框

class CEnigmaHardwareIDDlg : public CDialog
{
// 构造
public:
    explicit CEnigmaHardwareIDDlg(CWnd* pParent = nullptr);

    // RC4 相关
    void ExtractRC4EncryptionKeyFromRsa();
    int  m_sBox[256];           // S-Box（替换盒）
    int  a, b;
    unsigned char swap;
    void InitializeRC4Key(unsigned char* pKey, unsigned int lenKey);
    void RC4(unsigned char pData[], unsigned long lenData);

    // 字符串编解码
    int DecodeString(char* ToDecode, char* destination);
    int EncodeToString(char* ToEncode, int enlen, char* destination);

// 对话框数据
    enum { IDD = IDD_ENIGMAHARDWAREID_DIALOG };

// 重写
protected:
    virtual void DoDataExchange(CDataExchange* pDX);

// 实现
protected:
    HICON m_hIcon;

    // 消息处理函数
    virtual BOOL OnInitDialog();
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnGetbutton();
    afx_msg void OnGettrialkey();
    afx_msg void OnExtractrc4keyfromrsa();
    afx_msg void OnDecodehwid();
    afx_msg void OnGenhwid();

    DECLARE_MESSAGE_MAP()
};
