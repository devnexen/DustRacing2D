// This file is part of Dust Racing (DustRAC).
// Copyright (C) 2011 Jussi Lind <jussi.lind@iki.fi>
//
// DustRAC is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// DustRAC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DustRAC. If not, see <http://www.gnu.org/licenses/>.

#ifndef NEWTRACKDIALOG_HPP
#define NEWTRACKDIALOG_HPP

#include <QDialog>

class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;

//! Dialog displayed when user wants to create a new track.
class NewTrackDialog : public QDialog
{
    Q_OBJECT

public:

    //! Constructor.
    explicit NewTrackDialog(QWidget * parent = nullptr);

    //! Get column count.
    unsigned int cols() const;

    //! Get row count.
    unsigned int rows() const;

    //! Get name.
    QString name() const;

private:

    QGridLayout * m_layout;
    QPushButton * m_okButton;
    QPushButton * m_cancelButton;
    QLineEdit   * m_colsEdit;
    QLineEdit   * m_rowsEdit;
    QLineEdit   * m_nameEdit;
    QLabel      * m_colsLabel;
    QLabel      * m_rowsLabel;
    QLabel      * m_nameLabel;
};

#endif // NEWTRACKDIALOG_HPP
