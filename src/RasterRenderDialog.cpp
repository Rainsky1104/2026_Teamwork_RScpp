#include "rs/RasterRenderDialog.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <optional>

namespace rs {

std::optional<RasterRenderRequest> askRasterRenderRequest(QWidget* parent, const RasterLayer& raster)
{
    QDialog dlg(parent);
    dlg.setWindowTitle("渲染参数设置");
    QVBoxLayout* mainLay = new QVBoxLayout(&dlg);

    // 渲染模式下拉框
    QComboBox* cbxMode = new QComboBox();
    cbxMode->addItems({"AutoRgb", "RgbBands", "SingleBandGray", "PseudoColor"});
    mainLay->addWidget(new QLabel("渲染模式："));
    mainLay->addWidget(cbxMode);

    // 波段控件容器
    QWidget* bandWidget = new QWidget();
    QHBoxLayout* bandLay = new QHBoxLayout(bandWidget);
    QLabel* labR = new QLabel("R:");
    QLabel* labG = new QLabel("G:");
    QLabel* labB = new QLabel("B:");
    QLabel* labSingle = new QLabel("波段:");
    QSpinBox* spR = new QSpinBox();
    QSpinBox* spG = new QSpinBox();
    QSpinBox* spB = new QSpinBox();
    QSpinBox* spSingle = new QSpinBox();

    int maxBand = raster.bandCount() - 1;
    spR->setRange(0, maxBand);
    spG->setRange(0, maxBand);
    spB->setRange(0, maxBand);
    spSingle->setRange(0, maxBand);
    spR->setValue(0);
    spG->setValue(1);
    spB->setValue(2);
    spSingle->setValue(0);

    bandLay->addWidget(labR); bandLay->addWidget(spR);
    bandLay->addWidget(labG); bandLay->addWidget(spG);
    bandLay->addWidget(labB); bandLay->addWidget(spB);
    bandLay->addWidget(labSingle); bandLay->addWidget(spSingle);
    mainLay->addWidget(bandWidget);

    // 按钮
    QHBoxLayout* btnLay = new QHBoxLayout();
    QPushButton* btnOk = new QPushButton("确定");
    QPushButton* btnCancel = new QPushButton("取消");
    btnLay->addWidget(btnOk);
    btnLay->addWidget(btnCancel);
    mainLay->addLayout(btnLay);

    // 根据模式动态显隐控件
    auto updateVisible = [&]() {
        QString mode = cbxMode->currentText();
        bool rgbShow = (mode == "RgbBands");
        bool singleShow = (mode == "SingleBandGray" || mode == "PseudoColor");
        labR->setVisible(rgbShow); spR->setVisible(rgbShow);
        labG->setVisible(rgbShow); spG->setVisible(rgbShow);
        labB->setVisible(rgbShow); spB->setVisible(rgbShow);
        labSingle->setVisible(singleShow); spSingle->setVisible(singleShow);
    };
    updateVisible();
    QObject::connect(cbxMode, &QComboBox::currentTextChanged, updateVisible);

    QObject::connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted)
        return std::nullopt;

    // 填充返回结构体
    RasterRenderRequest req;
    QString modeStr = cbxMode->currentText();
    if (modeStr == "AutoRgb")
        req.mode = RasterRenderMode::AutoRgb;
    else if (modeStr == "RgbBands")
        req.mode = RasterRenderMode::RgbBands;
    else if (modeStr == "SingleBandGray")
        req.mode = RasterRenderMode::SingleBandGray;
    else if (modeStr == "PseudoColor")
        req.mode = RasterRenderMode::PseudoColor;

    req.redBand = spR->value();
    req.greenBand = spG->value();
    req.blueBand = spB->value();
    req.grayBand = spSingle->value();
    req.stretchToByte = true;   // 默认启用拉伸

    return req;
}

} // namespace rs