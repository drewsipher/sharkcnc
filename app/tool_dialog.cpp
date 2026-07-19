#include "tool_dialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "tool_library.h"

using namespace scnc;

ToolDialog::ToolDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Tool library");
    resize(640, 440);

    list_ = new QListWidget;
    list_->setMinimumWidth(220);

    auto* addBtn = new QPushButton("Add");
    auto* dupBtn = new QPushButton("Duplicate");
    auto* delBtn = new QPushButton("Remove");
    auto* listBtns = new QHBoxLayout;
    listBtns->addWidget(addBtn);
    listBtns->addWidget(dupBtn);
    listBtns->addWidget(delBtn);
    auto* leftCol = new QVBoxLayout;
    leftCol->addWidget(list_);
    leftCol->addLayout(listBtns);

    form_ = new QFormLayout;
    name_ = new QLineEdit;
    type_ = new QComboBox;
    type_->addItems({"End mill (flat)", "Ball nose", "Bull nose", "V-bit",
                     "Chamfer", "Drill", "Engraver"});
    auto mkD = [&](double lo, double hi, double step, const char* suffix) {
        auto* s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(3);
        s->setSingleStep(step);
        if (suffix) s->setSuffix(suffix);
        return s;
    };
    dia_ = mkD(0.01, 30, 0.1, " mm");
    shank_ = mkD(0.5, 30, 0.5, " mm");
    length_ = mkD(1, 200, 1, " mm");
    tipAngle_ = mkD(1, 179, 1, "\xC2\xB0");
    corner_ = mkD(0, 15, 0.1, " mm");
    flutes_ = new QSpinBox;
    flutes_->setRange(1, 12);
    feed_ = mkD(0, 5000, 10, " mm/min");
    plunge_ = mkD(0, 2000, 10, " mm/min");
    rpm_ = new QSpinBox;
    rpm_->setRange(0, 60000);
    rpm_->setSingleStep(500);
    notes_ = new QPlainTextEdit;
    notes_->setMaximumHeight(52);

    form_->addRow("Name", name_);
    form_->addRow("Type", type_);
    form_->addRow("Cutting Ø", dia_);
    form_->addRow("Shank Ø", shank_);
    form_->addRow("Cutting length", length_);
    tipRow_ = new QLabel("Tip angle");
    form_->addRow(tipRow_, tipAngle_);
    cornerRow_ = new QLabel("Corner radius");
    form_->addRow(cornerRow_, corner_);
    form_->addRow("Flutes", flutes_);
    form_->addRow("Feed", feed_);
    form_->addRow("Plunge", plunge_);
    form_->addRow("RPM", rpm_);
    form_->addRow("Notes", notes_);

    // live isolation-width preview
    auto* wrow = new QHBoxLayout;
    widthDepth_ = mkD(0.01, 5, 0.01, " mm deep");
    widthDepth_->setValue(0.06);
    widthPreview_ = new QLabel;
    wrow->addWidget(new QLabel("Cut width @"));
    wrow->addWidget(widthDepth_);
    wrow->addWidget(widthPreview_, 1);
    form_->addRow("Isolation", wrow);

    auto* rightCol = new QVBoxLayout;
    rightCol->addLayout(form_);
    rightCol->addStretch(1);
    auto* closeBtn = new QPushButton("Close");
    rightCol->addWidget(closeBtn, 0, Qt::AlignRight);

    auto* main = new QHBoxLayout(this);
    main->addLayout(leftCol);
    main->addLayout(rightCol, 1);

    connect(addBtn, &QPushButton::clicked, this, &ToolDialog::addTool);
    connect(dupBtn, &QPushButton::clicked, this, &ToolDialog::duplicateTool);
    connect(delBtn, &QPushButton::clicked, this, &ToolDialog::removeTool);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(list_, &QListWidget::currentRowChanged, this,
            &ToolDialog::selectRow);
    connect(type_, &QComboBox::currentIndexChanged, this, [this] {
        updateTypeVisibility();
        commitForm();
    });
    for (auto* s : {dia_, shank_, length_, tipAngle_, corner_, feed_, plunge_})
        connect(s, &QDoubleSpinBox::valueChanged, this,
                [this] { commitForm(); });
    connect(widthDepth_, &QDoubleSpinBox::valueChanged, this,
            [this] { updateWidthPreview(); });
    connect(flutes_, &QSpinBox::valueChanged, this, [this] { commitForm(); });
    connect(rpm_, &QSpinBox::valueChanged, this, [this] { commitForm(); });
    connect(name_, &QLineEdit::editingFinished, this,
            [this] { commitForm(); });
    connect(notes_, &QPlainTextEdit::textChanged, this,
            [this] { commitForm(); });

    refreshList();
    if (list_->count()) list_->setCurrentRow(0);
}

int ToolDialog::currentToolId() const {
    auto* it = list_->currentItem();
    return it ? it->data(Qt::UserRole).toInt() : -1;
}

void ToolDialog::refreshList(int selectId) {
    loading_ = true;
    list_->clear();
    for (const auto& t : ToolLibraryModel::instance().lib().tools()) {
        auto* it = new QListWidgetItem(QString::fromStdString(t.summary()));
        it->setData(Qt::UserRole, t.id);
        list_->addItem(it);
        if (t.id == selectId) list_->setCurrentItem(it);
    }
    loading_ = false;
}

void ToolDialog::selectRow(int) {
    const Tool* t = ToolLibraryModel::instance().lib().find(currentToolId());
    if (t) loadForm(*t);
}

void ToolDialog::loadForm(const Tool& t) {
    loading_ = true;
    name_->setText(QString::fromStdString(t.name));
    type_->setCurrentIndex(static_cast<int>(t.type));
    dia_->setValue(t.diameter);
    shank_->setValue(t.shankDiameter);
    length_->setValue(t.length);
    tipAngle_->setValue(t.tipAngle);
    corner_->setValue(t.cornerRadius);
    flutes_->setValue(t.flutes);
    feed_->setValue(t.feed);
    plunge_->setValue(t.plunge);
    rpm_->setValue(t.rpm);
    notes_->setPlainText(QString::fromStdString(t.notes));
    loading_ = false;
    updateTypeVisibility();
    updateWidthPreview();
}

void ToolDialog::updateTypeVisibility() {
    auto tt = static_cast<ToolType>(type_->currentIndex());
    bool coned = tt == ToolType::VBit || tt == ToolType::Chamfer ||
                 tt == ToolType::Engraver;
    bool rounded = tt == ToolType::BallNose || tt == ToolType::BullNose;
    tipRow_->setVisible(coned);
    tipAngle_->setVisible(coned);
    cornerRow_->setVisible(rounded);
    corner_->setVisible(rounded);
}

void ToolDialog::updateWidthPreview() {
    const Tool* t = ToolLibraryModel::instance().lib().find(currentToolId());
    if (!t) {
        widthPreview_->setText("-");
        return;
    }
    double w = t->widthAtDepth(widthDepth_->value());
    widthPreview_->setText(
        QString("<b>%1 mm</b> effective").arg(w, 0, 'f', 3));
}

void ToolDialog::commitForm() {
    if (loading_) return;
    Tool* t = ToolLibraryModel::instance().lib().find(currentToolId());
    if (!t) return;
    t->name = name_->text().toStdString();
    t->type = static_cast<ToolType>(type_->currentIndex());
    t->diameter = dia_->value();
    t->shankDiameter = shank_->value();
    t->length = length_->value();
    t->tipAngle = tipAngle_->value();
    t->cornerRadius = corner_->value();
    t->flutes = flutes_->value();
    t->feed = feed_->value();
    t->plunge = plunge_->value();
    t->rpm = rpm_->value();
    t->notes = notes_->toPlainText().toStdString();
    // refresh just the current row's label
    if (auto* it = list_->currentItem())
        it->setText(QString::fromStdString(t->summary()));
    updateWidthPreview();
    ToolLibraryModel::instance().notifyChanged();
}

void ToolDialog::addTool() {
    Tool t;
    t.name = "New tool";
    int id = ToolLibraryModel::instance().lib().add(t);
    ToolLibraryModel::instance().notifyChanged();
    refreshList(id);
}

void ToolDialog::duplicateTool() {
    const Tool* cur = ToolLibraryModel::instance().lib().find(currentToolId());
    if (!cur) return;
    Tool t = *cur;
    t.name += " copy";
    int id = ToolLibraryModel::instance().lib().add(t);
    ToolLibraryModel::instance().notifyChanged();
    refreshList(id);
}

void ToolDialog::removeTool() {
    int id = currentToolId();
    if (id < 0) return;
    ToolLibraryModel::instance().lib().remove(id);
    ToolLibraryModel::instance().notifyChanged();
    refreshList();
    if (list_->count()) list_->setCurrentRow(0);
}
