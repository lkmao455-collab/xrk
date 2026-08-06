#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include "hw/input_control.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xrk {

// Local, on-machine proof of the cross-keyboard-layout fix.
//
// The "host" text box below is our stand-in for the controlled computer. We
// force THIS thread's keyboard layout to something different from the physical
// one (e.g. Russian/Greek) to simulate a mismatched host. Then:
//   - "旧路径: 仅 VK"  injects via wVk only  -> output is scrambled by the
//     forced layout (exactly the reported bug).
//   - "新路径: Unicode" injects via KEYEVENTF_UNICODE -> output is always the
//     intended text, regardless of layout.
//
// Requires a real desktop session (SendInput needs a foreground window).
class KeyboardLayoutTestWindow : public QWidget {
    Q_OBJECT
public:
    KeyboardLayoutTestWindow() {
        auto* lay = new QVBoxLayout(this);
        setWindowTitle(tr("跨布局按键实测 (xrk)"));

        lay->addWidget(new QLabel(
            tr("下方文本框 = 被控机。先点一下让它获得焦点，再点底部按钮注入。")));
        m_result = new QLineEdit(this);
        m_result->setPlaceholderText(tr("注入结果显示在这里..."));
        lay->addWidget(m_result);

        lay->addWidget(new QLabel(
            tr("选择要模拟的“被控机键盘布局”(强制当前线程切换，模拟布局不一致):")));
        m_layoutCombo = new QComboBox(this);
        reloadLayouts();
        lay->addWidget(m_layoutCombo);

        auto* btnLay = new QHBoxLayout();
        m_oldBtn = new QPushButton(tr("旧路径: 仅 VK 注入 \"Test\""), this);
        m_newBtn = new QPushButton(tr("新路径: Unicode 注入 \"Test\""), this);
        m_clearBtn = new QPushButton(tr("清空"), this);
        btnLay->addWidget(m_oldBtn);
        btnLay->addWidget(m_newBtn);
        btnLay->addWidget(m_clearBtn);
        lay->addLayout(btnLay);

        lay->addWidget(new QLabel(
            tr("对比: 把布局切到与物理键盘不同(如俄语/希腊语)，旧路径会出乱码，"
               "新路径始终显示 Test。需在真实桌面运行。")));

        connect(m_oldBtn, &QPushButton::clicked, this, &KeyboardLayoutTestWindow::injectOld);
        connect(m_newBtn, &QPushButton::clicked, this, &KeyboardLayoutTestWindow::injectNew);
        connect(m_clearBtn, &QPushButton::clicked, m_result, &QLineEdit::clear);
        connect(m_layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &KeyboardLayoutTestWindow::onLayoutChanged);

        m_ic.initialize();
    }

private slots:
    void onLayoutChanged(int) {
#ifdef _WIN32
        HKL hkl = reinterpret_cast<HKL>(m_layoutCombo->currentData().toULongLong());
        ActivateKeyboardLayout(hkl, KLF_ACTIVATE);
#endif
        m_result->setFocus();
    }

    void injectOld() {
        m_result->setFocus();
        // Let focus settle before injecting.
        QTimer::singleShot(60, this, [this] {
            // VK_T, VK_E, VK_S, VK_T -> 旧路径(只用 wVk)
            for (uint32_t vk : {0x54u, 0x45u, 0x53u, 0x54u}) {
                m_ic.simulateKeyPress(vk);
                m_ic.simulateKeyRelease(vk);
            }
        });
    }

    void injectNew() {
        m_result->setFocus();
        QTimer::singleShot(60, this, [this] {
            m_ic.simulateText(QStringLiteral("Test"));  // 新路径(Unicode)
        });
    }

private:
    void reloadLayouts() {
#ifdef _WIN32
        m_layoutCombo->clear();
        UINT n = GetKeyboardLayoutList(0, nullptr);
        if (n == 0) return;
        QVector<HKL> list(static_cast<int>(n));
        GetKeyboardLayoutList(n, list.data());
        for (HKL hkl : list) {
            char name[KL_NAMELENGTH] = {0};
            QString label;
            if (GetKeyboardLayoutNameA(name)) {
                label = QString("KLID=%1").arg(name);
            }
            label += QString(" HKL=0x%1").arg(
                reinterpret_cast<quintptr>(hkl), 0, 16);
            m_layoutCombo->addItem(label,
                static_cast<quint64>(reinterpret_cast<quintptr>(hkl)));
        }
#else
        m_layoutCombo->addItem("非 Windows: 仅演示");
#endif
    }

    QLineEdit* m_result = nullptr;
    QComboBox* m_layoutCombo = nullptr;
    QPushButton* m_oldBtn = nullptr;
    QPushButton* m_newBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    InputControl m_ic;
};

} // namespace xrk

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    xrk::KeyboardLayoutTestWindow w;
    w.show();
    return app.exec();
}

#include "keyboard_layout_test.moc"
