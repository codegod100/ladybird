/*
 * Experimental password manager dialog (OpenBao KV backed).
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <QDialog>

class QTableWidget;
class QLineEdit;

namespace Ladybird {

class PasswordManagerDialog final : public QDialog {
    Q_OBJECT
public:
    explicit PasswordManagerDialog(QWidget* parent = nullptr);

private slots:
    void refresh();
    void add_password();
    void remove_selected();
    void copy_selected_password();

private:
    QTableWidget* m_table { nullptr };
    QLineEdit* m_origin { nullptr };
    QLineEdit* m_username { nullptr };
    QLineEdit* m_password { nullptr };
};

}
