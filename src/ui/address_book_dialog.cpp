#include "address_book_dialog.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>

namespace xrk {

AddressBookDialog::AddressBookDialog(QWidget* parent)
    : QDialog(parent) {
    setupUI();
}

AddressBookDialog::AddressBookDialog(const AddressBookEntry& entry, QWidget* parent)
    : QDialog(parent) {
    setupUI();
    populate(entry);
}

AddressBookEntry AddressBookDialog::entry() const {
    AddressBookEntry e;
    e.id = m_entryId;
    e.name = m_nameEdit->text().trimmed();
    e.ip = m_ipEdit->text().trimmed();
    e.port = static_cast<uint16_t>(m_portSpinBox->value());
    e.mac = m_macEdit->text().trimmed();
    e.group = m_groupEdit->text().trimmed();
    e.accessCode = m_codeEdit->text().trimmed();
    e.notes = m_notesEdit->text().trimmed();
    e.favorite = m_favoriteCheck->isChecked();
    return e;
}

void AddressBookDialog::setupUI() {
    setWindowTitle("地址簿 - 添加设备");
    setMinimumWidth(380);

    auto* form = new QFormLayout();

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("设备别名 (如: 办公室电脑)");
    form->addRow("名称:", m_nameEdit);

    m_ipEdit = new QLineEdit(this);
    m_ipEdit->setPlaceholderText("IP地址");
    form->addRow("地址:", m_ipEdit);

    m_portSpinBox = new QSpinBox(this);
    m_portSpinBox->setRange(1, 65535);
    m_portSpinBox->setValue(9999);
    form->addRow("端口:", m_portSpinBox);

    m_macEdit = new QLineEdit(this);
    m_macEdit->setPlaceholderText("MAC (用于WOL唤醒)");
    form->addRow("MAC:", m_macEdit);

    m_groupEdit = new QLineEdit(this);
    m_groupEdit->setPlaceholderText("分组 (如: 办公室/家庭/服务器)");
    form->addRow("分组:", m_groupEdit);

    m_codeEdit = new QLineEdit(this);
    m_codeEdit->setPlaceholderText("识别码 (可选)");
    form->addRow("识别码:", m_codeEdit);

    m_notesEdit = new QLineEdit(this);
    m_notesEdit->setPlaceholderText("备注...");
    form->addRow("备注:", m_notesEdit);

    m_favoriteCheck = new QCheckBox("设为收藏", this);
    form->addRow("", m_favoriteCheck);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void AddressBookDialog::populate(const AddressBookEntry& entry) {
    m_entryId = entry.id;
    setWindowTitle("编辑设备 - " + entry.name);
    m_nameEdit->setText(entry.name);
    m_ipEdit->setText(entry.ip);
    m_portSpinBox->setValue(entry.port);
    m_macEdit->setText(entry.mac);
    m_groupEdit->setText(entry.group);
    m_codeEdit->setText(entry.accessCode);
    m_notesEdit->setText(entry.notes);
    m_favoriteCheck->setChecked(entry.favorite);
}

} // namespace xrk
