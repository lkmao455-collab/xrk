#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include "app/address_book.h"

namespace xrk {

class AddressBookDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddressBookDialog(QWidget* parent = nullptr);
    explicit AddressBookDialog(const AddressBookEntry& entry, QWidget* parent = nullptr);

    AddressBookEntry entry() const;

private:
    void setupUI();
    void populate(const AddressBookEntry& entry);

    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_ipEdit = nullptr;
    QSpinBox* m_portSpinBox = nullptr;
    QLineEdit* m_macEdit = nullptr;
    QLineEdit* m_groupEdit = nullptr;
    QLineEdit* m_codeEdit = nullptr;
    QLineEdit* m_notesEdit = nullptr;
    QCheckBox* m_favoriteCheck = nullptr;
    QString m_entryId;
};

} // namespace xrk
