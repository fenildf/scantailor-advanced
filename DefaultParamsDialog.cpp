
#include "DefaultParamsDialog.h"
#include <filters/output/ColorParams.h>
#include <filters/output/DewarpingOptions.h>
#include <filters/output/PictureShapeOptions.h>
#include <filters/page_split/LayoutType.h>
#include <foundation/ScopedIncDec.h>
#include <QLineEdit>
#include <QtCore/QSettings>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QToolTip>
#include <cassert>
#include <memory>
#include "DefaultParamsProvider.h"
#include "UnitsProvider.h"
#include "Utils.h"

using namespace page_split;
using namespace output;
using namespace page_layout;

DefaultParamsDialog::DefaultParamsDialog(QWidget* parent)
    : QDialog(parent),
      m_leftRightLinkEnabled(true),
      m_topBottomLinkEnabled(true),
      m_ignoreMarginChanges(0),
      m_currentUnits(MILLIMETRES),
      m_ignoreProfileChanges(0) {
  setupUi(this);

  layoutModeCB->addItem(tr("Auto"), MODE_AUTO);
  layoutModeCB->addItem(tr("Manual"), MODE_MANUAL);

  colorModeSelector->addItem(tr("Black and White"), BLACK_AND_WHITE);
  colorModeSelector->addItem(tr("Color / Grayscale"), COLOR_GRAYSCALE);
  colorModeSelector->addItem(tr("Mixed"), MIXED);

  fillingColorBox->addItem(tr("Background"), FILL_BACKGROUND);
  fillingColorBox->addItem(tr("White"), FILL_WHITE);

  thresholdMethodBox->addItem(tr("Otsu"), OTSU);
  thresholdMethodBox->addItem(tr("Sauvola"), SAUVOLA);
  thresholdMethodBox->addItem(tr("Wolf"), WOLF);

  pictureShapeSelector->addItem(tr("Off"), OFF_SHAPE);
  pictureShapeSelector->addItem(tr("Free"), FREE_SHAPE);
  pictureShapeSelector->addItem(tr("Rectangular"), RECTANGULAR_SHAPE);

  dpiSelector->addItem("300", "300");
  dpiSelector->addItem("400", "400");
  dpiSelector->addItem("600", "600");
  m_customDpiItemIdx = dpiSelector->count();
  m_customDpiValue = "200";
  dpiSelector->addItem(tr("Custom"), m_customDpiValue);

  dewarpingModeCB->addItem(tr("Off"), OFF);
  dewarpingModeCB->addItem(tr("Auto"), AUTO);
  dewarpingModeCB->addItem(tr("Manual"), MANUAL);
  dewarpingModeCB->addItem(tr("Marginal"), MARGINAL);

  m_reservedProfileNames.insert("Default");
  m_reservedProfileNames.insert("Source");
  m_reservedProfileNames.insert("Custom");

  profileCB->addItem(tr("Default"), "Default");
  profileCB->addItem(tr("Source"), "Source");
  const std::list<QString> profileList = m_profileManager.getProfileList();
  for (const QString& profileName : profileList) {
    if (!isProfileNameReserved(profileName)) {
      profileCB->addItem(profileName, profileName);
    }
  }
  m_customProfileItemIdx = profileCB->count();
  profileCB->addItem(tr("Custom"), "Custom");

  m_reservedProfileNames.insert(profileCB->itemText(profileCB->findData("Default")));
  m_reservedProfileNames.insert(profileCB->itemText(profileCB->findData("Source")));
  m_reservedProfileNames.insert(profileCB->itemText(profileCB->findData("Custom")));

  m_chainIcon.addPixmap(QPixmap(QString::fromLatin1(":/icons/stock-vchain-24.png")));
  m_brokenChainIcon.addPixmap(QPixmap(QString::fromLatin1(":/icons/stock-vchain-broken-24.png")));
  setLinkButtonLinked(topBottomLink, m_topBottomLinkEnabled);
  setLinkButtonLinked(leftRightLink, m_leftRightLinkEnabled);

  Utils::mapSetValue(m_alignmentByButton, alignTopLeftBtn, Alignment(Alignment::TOP, Alignment::LEFT));
  Utils::mapSetValue(m_alignmentByButton, alignTopBtn, Alignment(Alignment::TOP, Alignment::HCENTER));
  Utils::mapSetValue(m_alignmentByButton, alignTopRightBtn, Alignment(Alignment::TOP, Alignment::RIGHT));
  Utils::mapSetValue(m_alignmentByButton, alignLeftBtn, Alignment(Alignment::VCENTER, Alignment::LEFT));
  Utils::mapSetValue(m_alignmentByButton, alignCenterBtn, Alignment(Alignment::VCENTER, Alignment::HCENTER));
  Utils::mapSetValue(m_alignmentByButton, alignRightBtn, Alignment(Alignment::VCENTER, Alignment::RIGHT));
  Utils::mapSetValue(m_alignmentByButton, alignBottomLeftBtn, Alignment(Alignment::BOTTOM, Alignment::LEFT));
  Utils::mapSetValue(m_alignmentByButton, alignBottomBtn, Alignment(Alignment::BOTTOM, Alignment::HCENTER));
  Utils::mapSetValue(m_alignmentByButton, alignBottomRightBtn, Alignment(Alignment::BOTTOM, Alignment::RIGHT));

  m_alignmentButtonGroup = std::make_unique<QButtonGroup>(this);
  for (const auto& buttonAndAlignment : m_alignmentByButton) {
    m_alignmentButtonGroup->addButton(buttonAndAlignment.first);
  }

  darkerThresholdLink->setText(Utils::richTextForLink(darkerThresholdLink->text()));
  lighterThresholdLink->setText(Utils::richTextForLink(lighterThresholdLink->text()));
  thresholdSlider->setToolTip(QString::number(thresholdSlider->value()));

  thresholdSlider->setMinimum(-100);
  thresholdSlider->setMaximum(100);
  thresholLabel->setText(QString::number(thresholdSlider->value()));

  depthPerceptionSlider->setMinimum(qRound(DepthPerception::minValue() * 10));
  depthPerceptionSlider->setMaximum(qRound(DepthPerception::maxValue() * 10));

  despeckleSlider->setMinimum(qRound(1.0 * 10));
  despeckleSlider->setMaximum(qRound(3.0 * 10));

  const int index = profileCB->findData(DefaultParamsProvider::getInstance()->getProfileName());
  if (index != -1) {
    profileCB->setCurrentIndex(index);
  }
  profileChanged(profileCB->currentIndex());

  connect(buttonBox, SIGNAL(accepted()), this, SLOT(commitChanges()));
}

void DefaultParamsDialog::updateFixOrientationDisplay(const DefaultParams::FixOrientationParams& params) {
  m_orthogonalRotation = params.getImageRotation();
  setRotationPixmap();
}

void DefaultParamsDialog::updatePageSplitDisplay(const DefaultParams::PageSplitParams& params) {
  LayoutType layoutType = params.getLayoutType();
  if (layoutType == AUTO_LAYOUT_TYPE) {
    layoutModeCB->setCurrentIndex(layoutModeCB->findData(static_cast<int>(MODE_AUTO)));
    pageLayoutGroup->setEnabled(false);
  } else {
    layoutModeCB->setCurrentIndex(layoutModeCB->findData(static_cast<int>(MODE_MANUAL)));
    pageLayoutGroup->setEnabled(true);
  }

  switch (layoutType) {
    case AUTO_LAYOUT_TYPE:
      // Uncheck all buttons.  Can only be done
      // by playing with exclusiveness.
      twoPagesBtn->setChecked(true);
      twoPagesBtn->setAutoExclusive(false);
      twoPagesBtn->setChecked(false);
      twoPagesBtn->setAutoExclusive(true);
      break;
    case SINGLE_PAGE_UNCUT:
      singlePageUncutBtn->setChecked(true);
      break;
    case PAGE_PLUS_OFFCUT:
      pagePlusOffcutBtn->setChecked(true);
      break;
    case TWO_PAGES:
      twoPagesBtn->setChecked(true);
      break;
  }
}

void DefaultParamsDialog::updateDeskewDisplay(const DefaultParams::DeskewParams& params) {
  AutoManualMode mode = params.getMode();
  if (mode == MODE_AUTO) {
    deskewAutoBtn->setChecked(true);
  } else {
    deskewManualBtn->setChecked(true);
  }
  angleSpinBox->setEnabled(mode == MODE_MANUAL);
  angleSpinBox->setValue(params.getDeskewAngleDeg());
}

void DefaultParamsDialog::updateSelectContentDisplay(const DefaultParams::SelectContentParams& params) {
  const AutoManualMode pageDetectMode = params.getPageDetectMode();
  pageDetectOptions->setEnabled(pageDetectMode != MODE_DISABLED);
  fineTuneBtn->setEnabled(pageDetectMode == MODE_AUTO);
  dimensionsWidget->setEnabled(pageDetectMode == MODE_MANUAL);
  switch (pageDetectMode) {
    case MODE_AUTO:
      pageDetectAutoBtn->setChecked(true);
      break;
    case MODE_MANUAL:
      pageDetectManualBtn->setChecked(true);
      break;
    case MODE_DISABLED:
      pageDetectDisableBtn->setChecked(true);
      break;
  }

  if (params.isContentDetectEnabled()) {
    contentDetectAutoBtn->setChecked(true);
  } else {
    contentDetectDisableBtn->setChecked(true);
  }

  QSizeF pageRectSize = params.getPageRectSize();
  widthSpinBox->setValue(pageRectSize.width());
  heightSpinBox->setValue(pageRectSize.height());
  fineTuneBtn->setChecked(params.isFineTuneCorners());
}

void DefaultParamsDialog::updatePageLayoutDisplay(const DefaultParams::PageLayoutParams& params) {
  autoMargins->setChecked(params.isAutoMargins());
  marginsWidget->setEnabled(!params.isAutoMargins());

  const Margins& margins = params.getHardMargins();
  topMarginSpinBox->setValue(margins.top());
  rightMarginSpinBox->setValue(margins.right());
  bottomMarginSpinBox->setValue(margins.bottom());
  leftMarginSpinBox->setValue(margins.left());

  m_topBottomLinkEnabled = (margins.top() == margins.bottom());
  m_leftRightLinkEnabled = (margins.left() == margins.right());
  setLinkButtonLinked(topBottomLink, m_topBottomLinkEnabled);
  setLinkButtonLinked(leftRightLink, m_leftRightLinkEnabled);

  const Alignment& alignment = params.getAlignment();
  if (alignment.isAuto()) {
    alignmentMode->setCurrentIndex(0);
    autoAlignSettingsGroup->setVisible(true);
    autoVerticalAligningCB->setChecked(alignment.isAutoVertical());
    autoHorizontalAligningCB->setChecked(alignment.isAutoHorizontal());
  } else if (alignment.isOriginal()) {
    alignmentMode->setCurrentIndex(2);
    autoAlignSettingsGroup->setVisible(true);
    autoVerticalAligningCB->setChecked(alignment.isAutoVertical());
    autoHorizontalAligningCB->setChecked(alignment.isAutoHorizontal());
  } else {
    alignmentMode->setCurrentIndex(1);
    autoAlignSettingsGroup->setVisible(false);
  }
  alignWithOthersCB->setChecked(!alignment.isNull());

  for (const auto& kv : m_alignmentByButton) {
    if (alignment.isAuto() || alignment.isOriginal()) {
      if (!alignment.isAutoHorizontal() && (kv.second.vertical() == Alignment::VCENTER)
          && (kv.second.horizontal() == alignment.horizontal())) {
        kv.first->setChecked(true);
        break;
      } else if (!alignment.isAutoVertical() && (kv.second.horizontal() == Alignment::HCENTER)
                 && (kv.second.vertical() == alignment.vertical())) {
        kv.first->setChecked(true);
        break;
      }
    } else if (kv.second == alignment) {
      kv.first->setChecked(true);
      break;
    }
  }

  updateAlignmentModeEnabled();
  updateAlignmentButtonsEnabled();
  updateAutoModeButtons();
}

void DefaultParamsDialog::updateOutputDisplay(const DefaultParams::OutputParams& params) {
  const ColorParams& colorParams = params.getColorParams();
  colorModeSelector->setCurrentIndex(colorModeSelector->findData(colorParams.colorMode()));

  const ColorCommonOptions& colorCommonOptions = colorParams.colorCommonOptions();
  const BlackWhiteOptions& blackWhiteOptions = colorParams.blackWhiteOptions();
  fillMarginsCB->setChecked(colorCommonOptions.fillMargins());
  fillOffcutCB->setChecked(colorCommonOptions.fillOffcut());
  equalizeIlluminationCB->setChecked(blackWhiteOptions.normalizeIllumination());
  equalizeIlluminationColorCB->setChecked(colorCommonOptions.normalizeIllumination());
  savitzkyGolaySmoothingCB->setChecked(blackWhiteOptions.isSavitzkyGolaySmoothingEnabled());
  morphologicalSmoothingCB->setChecked(blackWhiteOptions.isMorphologicalSmoothingEnabled());

  fillingColorBox->setCurrentIndex(fillingColorBox->findData(colorCommonOptions.getFillingColor()));

  colorSegmentationCB->setChecked(blackWhiteOptions.getColorSegmenterOptions().isEnabled());
  reduceNoiseSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getNoiseReduction());
  redAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getRedThresholdAdjustment());
  greenAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getGreenThresholdAdjustment());
  blueAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getBlueThresholdAdjustment());
  posterizeCB->setChecked(colorCommonOptions.getPosterizationOptions().isEnabled());
  posterizeLevelSB->setValue(colorCommonOptions.getPosterizationOptions().getLevel());
  posterizeNormalizationCB->setChecked(colorCommonOptions.getPosterizationOptions().isNormalizationEnabled());
  posterizeForceBwCB->setChecked(colorCommonOptions.getPosterizationOptions().isForceBlackAndWhite());

  thresholdMethodBox->setCurrentIndex(thresholdMethodBox->findData(blackWhiteOptions.getBinarizationMethod()));
  thresholdSlider->setValue(blackWhiteOptions.thresholdAdjustment());
  thresholLabel->setText(QString::number(thresholdSlider->value()));
  sauvolaWindowSize->setValue(blackWhiteOptions.getWindowSize());
  wolfWindowSize->setValue(blackWhiteOptions.getWindowSize());
  sauvolaCoef->setValue(blackWhiteOptions.getSauvolaCoef());
  lowerBound->setValue(blackWhiteOptions.getWolfLowerBound());
  upperBound->setValue(blackWhiteOptions.getWolfUpperBound());
  wolfCoef->setValue(blackWhiteOptions.getWolfCoef());

  const PictureShapeOptions& pictureShapeOptions = params.getPictureShapeOptions();
  pictureShapeSelector->setCurrentIndex(pictureShapeSelector->findData(pictureShapeOptions.getPictureShape()));
  pictureShapeSensitivitySB->setValue(pictureShapeOptions.getSensitivity());
  higherSearchSensitivityCB->setChecked(pictureShapeOptions.isHigherSearchSensitivity());

  int dpiIndex = dpiSelector->findData(QString::number(params.getDpi().vertical()));
  if (dpiIndex != -1) {
    dpiSelector->setCurrentIndex(dpiIndex);
  } else {
    dpiSelector->setCurrentIndex(m_customDpiItemIdx);
    m_customDpiValue = QString::number(params.getDpi().vertical());
  }

  const SplittingOptions& splittingOptions = params.getSplittingOptions();
  splittingCB->setChecked(splittingOptions.isSplitOutput());
  switch (splittingOptions.getSplittingMode()) {
    case BLACK_AND_WHITE_FOREGROUND:
      bwForegroundRB->setChecked(true);
      break;
    case COLOR_FOREGROUND:
      colorForegroundRB->setChecked(true);
      break;
  }
  originalBackgroundCB->setChecked(splittingOptions.isOriginalBackgroundEnabled());

  const double despeckleLevel = params.getDespeckleLevel();
  if (despeckleLevel != 0) {
    despeckleCB->setChecked(true);
    despeckleSlider->setValue(qRound(10 * despeckleLevel));
  } else {
    despeckleCB->setChecked(false);
  }
  despeckleSlider->setToolTip(QString::number(0.1 * despeckleSlider->value()));

  dewarpingModeCB->setCurrentIndex(dewarpingModeCB->findData(params.getDewarpingOptions().dewarpingMode()));
  dewarpingPostDeskewCB->setChecked(params.getDewarpingOptions().needPostDeskew());
  depthPerceptionSlider->setValue(qRound(params.getDepthPerception().value() * 10));

  // update the display
  colorModeChanged(colorModeSelector->currentIndex());
  thresholdMethodChanged(thresholdMethodBox->currentIndex());
  pictureShapeChanged(pictureShapeSelector->currentIndex());
  equalizeIlluminationToggled(equalizeIlluminationCB->isChecked());
  splittingToggled(splittingCB->isChecked());
  dpiSelectionChanged(dpiSelector->currentIndex());
  despeckleToggled(despeckleCB->isChecked());
}

#define CONNECT(...) m_connectionList.push_back(connect(__VA_ARGS__));

void DefaultParamsDialog::setupUiConnections() {
  CONNECT(rotateLeftBtn, SIGNAL(clicked()), this, SLOT(rotateLeft()));
  CONNECT(rotateRightBtn, SIGNAL(clicked()), this, SLOT(rotateRight()));
  CONNECT(resetBtn, SIGNAL(clicked()), this, SLOT(resetRotation()));
  CONNECT(layoutModeCB, SIGNAL(currentIndexChanged(int)), this, SLOT(layoutModeChanged(int)));
  CONNECT(deskewAutoBtn, SIGNAL(toggled(bool)), this, SLOT(deskewModeChanged(bool)));
  CONNECT(pageDetectAutoBtn, SIGNAL(pressed()), this, SLOT(pageDetectAutoToggled()));
  CONNECT(pageDetectManualBtn, SIGNAL(pressed()), this, SLOT(pageDetectManualToggled()));
  CONNECT(pageDetectDisableBtn, SIGNAL(pressed()), this, SLOT(pageDetectDisableToggled()));
  CONNECT(autoMargins, SIGNAL(toggled(bool)), this, SLOT(autoMarginsToggled(bool)));
  CONNECT(alignmentMode, SIGNAL(currentIndexChanged(int)), this, SLOT(alignmentModeChanged(int)));
  CONNECT(alignWithOthersCB, SIGNAL(toggled(bool)), this, SLOT(alignWithOthersToggled(bool)));
  CONNECT(topBottomLink, SIGNAL(clicked()), this, SLOT(topBottomLinkClicked()));
  CONNECT(leftRightLink, SIGNAL(clicked()), this, SLOT(leftRightLinkClicked()));
  CONNECT(topMarginSpinBox, SIGNAL(valueChanged(double)), this, SLOT(vertMarginsChanged(double)));
  CONNECT(bottomMarginSpinBox, SIGNAL(valueChanged(double)), this, SLOT(vertMarginsChanged(double)));
  CONNECT(leftMarginSpinBox, SIGNAL(valueChanged(double)), this, SLOT(horMarginsChanged(double)));
  CONNECT(rightMarginSpinBox, SIGNAL(valueChanged(double)), this, SLOT(horMarginsChanged(double)));
  CONNECT(colorModeSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(colorModeChanged(int)));
  CONNECT(thresholdMethodBox, SIGNAL(currentIndexChanged(int)), this, SLOT(thresholdMethodChanged(int)));
  CONNECT(pictureShapeSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(pictureShapeChanged(int)));
  CONNECT(equalizeIlluminationCB, SIGNAL(clicked(bool)), this, SLOT(equalizeIlluminationToggled(bool)));
  CONNECT(splittingCB, SIGNAL(clicked(bool)), this, SLOT(splittingToggled(bool)));
  CONNECT(bwForegroundRB, SIGNAL(clicked(bool)), this, SLOT(bwForegroundToggled(bool)));
  CONNECT(colorForegroundRB, SIGNAL(clicked(bool)), this, SLOT(colorForegroundToggled(bool)));
  CONNECT(lighterThresholdLink, SIGNAL(linkActivated(const QString&)), this, SLOT(setLighterThreshold()));
  CONNECT(darkerThresholdLink, SIGNAL(linkActivated(const QString&)), this, SLOT(setDarkerThreshold()));
  CONNECT(thresholdSlider, SIGNAL(valueChanged(int)), this, SLOT(thresholdSliderValueChanged(int)));
  CONNECT(neutralThresholdBtn, SIGNAL(clicked()), this, SLOT(setNeutralThreshold()));
  CONNECT(dpiSelector, SIGNAL(activated(int)), this, SLOT(dpiSelectionChanged(int)));
  CONNECT(dpiSelector, SIGNAL(editTextChanged(const QString&)), this, SLOT(dpiEditTextChanged(const QString&)));
  CONNECT(depthPerceptionSlider, SIGNAL(valueChanged(int)), this, SLOT(depthPerceptionChangedSlot(int)));
  CONNECT(profileCB, SIGNAL(currentIndexChanged(int)), this, SLOT(profileChanged(int)));
  CONNECT(profileSaveButton, SIGNAL(pressed()), this, SLOT(profileSavePressed()));
  CONNECT(profileDeleteButton, SIGNAL(pressed()), this, SLOT(profileDeletePressed()));
  CONNECT(colorSegmentationCB, SIGNAL(clicked(bool)), this, SLOT(colorSegmentationToggled(bool)));
  CONNECT(posterizeCB, SIGNAL(clicked(bool)), this, SLOT(posterizeToggled(bool)));
  CONNECT(autoHorizontalAligningCB, SIGNAL(toggled(bool)), this, SLOT(autoHorizontalAligningToggled(bool)));
  CONNECT(autoVerticalAligningCB, SIGNAL(toggled(bool)), this, SLOT(autoVerticalAligningToggled(bool)));
  CONNECT(despeckleCB, SIGNAL(clicked(bool)), this, SLOT(despeckleToggled(bool)));
  CONNECT(despeckleSlider, SIGNAL(valueChanged(int)), this, SLOT(despeckleSliderValueChanged(int)));
}

#undef CONNECT

void DefaultParamsDialog::removeUiConnections() {
  for (const auto& connection : m_connectionList) {
    disconnect(connection);
  }
  m_connectionList.clear();
}

void DefaultParamsDialog::rotateLeft() {
  OrthogonalRotation rotation(m_orthogonalRotation);
  rotation.prevClockwiseDirection();
  setRotation(rotation);
}

void DefaultParamsDialog::rotateRight() {
  OrthogonalRotation rotation(m_orthogonalRotation);
  rotation.nextClockwiseDirection();
  setRotation(rotation);
}

void DefaultParamsDialog::resetRotation() {
  setRotation(OrthogonalRotation());
}

void DefaultParamsDialog::setRotation(const OrthogonalRotation& rotation) {
  if (rotation == m_orthogonalRotation) {
    return;
  }

  m_orthogonalRotation = rotation;
  setRotationPixmap();
}

void DefaultParamsDialog::setRotationPixmap() {
  const char* path = nullptr;

  switch (m_orthogonalRotation.toDegrees()) {
    case 0:
      path = ":/icons/big-up-arrow.png";
      break;
    case 90:
      path = ":/icons/big-right-arrow.png";
      break;
    case 180:
      path = ":/icons/big-down-arrow.png";
      break;
    case 270:
      path = ":/icons/big-left-arrow.png";
      break;
    default:
      assert(!"Unreachable");
  }

  rotationIndicator->setPixmap(QPixmap(path));
}

void DefaultParamsDialog::layoutModeChanged(const int idx) {
  const AutoManualMode mode = static_cast<AutoManualMode>(layoutModeCB->itemData(idx).toInt());
  if (mode == MODE_AUTO) {
    // Uncheck all buttons.  Can only be done
    // by playing with exclusiveness.
    twoPagesBtn->setChecked(true);
    twoPagesBtn->setAutoExclusive(false);
    twoPagesBtn->setChecked(false);
    twoPagesBtn->setAutoExclusive(true);
  } else {
    singlePageUncutBtn->setChecked(true);
  }
  pageLayoutGroup->setEnabled(mode == MODE_MANUAL);
}

void DefaultParamsDialog::deskewModeChanged(const bool auto_mode) {
  angleSpinBox->setEnabled(!auto_mode);
}

void DefaultParamsDialog::pageDetectAutoToggled() {
  pageDetectOptions->setEnabled(true);
  fineTuneBtn->setEnabled(true);
  dimensionsWidget->setEnabled(false);
}

void DefaultParamsDialog::pageDetectManualToggled() {
  pageDetectOptions->setEnabled(true);
  fineTuneBtn->setEnabled(false);
  dimensionsWidget->setEnabled(true);
}

void DefaultParamsDialog::pageDetectDisableToggled() {
  pageDetectOptions->setEnabled(false);
}

void DefaultParamsDialog::autoMarginsToggled(const bool checked) {
  marginsWidget->setEnabled(!checked);
}

void DefaultParamsDialog::alignmentModeChanged(const int idx) {
  autoAlignSettingsGroup->setVisible((idx == 0) || (idx == 2));
  updateAlignmentButtonsEnabled();
  updateAutoModeButtons();
}

void DefaultParamsDialog::alignWithOthersToggled(const bool state) {
  updateAlignmentModeEnabled();
  updateAlignmentButtonsEnabled();
}

void DefaultParamsDialog::colorModeChanged(const int idx) {
  const auto colorMode = static_cast<ColorMode>(colorModeSelector->itemData(idx).toInt());
  bool threshold_options_visible = false;
  bool picture_shape_visible = false;
  bool splitting_options_visible = false;
  switch (colorMode) {
    case MIXED:
      picture_shape_visible = true;
      splitting_options_visible = true;
      // fall into
    case BLACK_AND_WHITE:
      threshold_options_visible = true;
      // fall into
    case COLOR_GRAYSCALE:
      break;
  }
  thresholdOptions->setEnabled(threshold_options_visible);
  despecklePanel->setEnabled(threshold_options_visible);
  pictureShapeOptions->setEnabled(picture_shape_visible);
  splittingOptions->setEnabled(splitting_options_visible);

  fillingOptions->setEnabled(colorMode != BLACK_AND_WHITE);

  equalizeIlluminationCB->setEnabled(colorMode != COLOR_GRAYSCALE);
  equalizeIlluminationColorCB->setEnabled(colorMode != BLACK_AND_WHITE);
  if ((colorMode == MIXED)) {
    if (equalizeIlluminationColorCB->isChecked()) {
      equalizeIlluminationColorCB->setChecked(equalizeIlluminationCB->isChecked());
    }
    equalizeIlluminationColorCB->setEnabled(equalizeIlluminationCB->isChecked());
  }
  savitzkyGolaySmoothingCB->setEnabled(colorMode != COLOR_GRAYSCALE);
  morphologicalSmoothingCB->setEnabled(colorMode != COLOR_GRAYSCALE);

  colorSegmentationCB->setEnabled(threshold_options_visible);
  segmenterOptionsWidget->setEnabled(threshold_options_visible && colorSegmentationCB->isChecked());
  if (threshold_options_visible) {
    posterizeCB->setEnabled(colorSegmentationCB->isChecked());
    posterizeOptionsWidget->setEnabled(colorSegmentationCB->isChecked() && posterizeCB->isChecked());
  } else {
    posterizeCB->setEnabled(true);
    posterizeOptionsWidget->setEnabled(posterizeCB->isChecked());
  }
}

void DefaultParamsDialog::thresholdMethodChanged(const int idx) {
  binarizationOptions->setCurrentIndex(idx);
}

void DefaultParamsDialog::pictureShapeChanged(const int idx) {
  const auto shapeMode = static_cast<PictureShape>(pictureShapeSelector->itemData(idx).toInt());
  pictureShapeSensitivityOptions->setEnabled(shapeMode == RECTANGULAR_SHAPE);
  higherSearchSensitivityCB->setEnabled(shapeMode != OFF_SHAPE);
}

void DefaultParamsDialog::equalizeIlluminationToggled(const bool checked) {
  const auto colorMode = static_cast<ColorMode>(colorModeSelector->currentData().toInt());
  if (colorMode == MIXED) {
    if (equalizeIlluminationColorCB->isChecked()) {
      equalizeIlluminationColorCB->setChecked(checked);
    }
    equalizeIlluminationColorCB->setEnabled(checked);
  }
}

void DefaultParamsDialog::splittingToggled(const bool checked) {
  bwForegroundRB->setEnabled(checked);
  colorForegroundRB->setEnabled(checked);
  originalBackgroundCB->setEnabled(checked && bwForegroundRB->isChecked());
}

void DefaultParamsDialog::bwForegroundToggled(bool checked) {
  if (!checked) {
    return;
  }
  originalBackgroundCB->setEnabled(checked);
}

void DefaultParamsDialog::colorForegroundToggled(bool checked) {
  if (!checked) {
    return;
  }
  originalBackgroundCB->setEnabled(!checked);
}

void DefaultParamsDialog::loadParams(const DefaultParams& params) {
  removeUiConnections();

  // must be done before updating the displays in order to set the precise of the spin boxes
  updateUnits(params.getUnits());

  updateFixOrientationDisplay(params.getFixOrientationParams());
  updatePageSplitDisplay(params.getPageSplitParams());
  updateDeskewDisplay(params.getDeskewParams());
  updateSelectContentDisplay(params.getSelectContentParams());
  updatePageLayoutDisplay(params.getPageLayoutParams());
  updateOutputDisplay(params.getOutputParams());

  setupUiConnections();
}

std::unique_ptr<DefaultParams> DefaultParamsDialog::buildParams() const {
  DefaultParams::FixOrientationParams fixOrientationParams(m_orthogonalRotation);

  LayoutType layoutType;
  if (layoutModeCB->currentData() == MODE_AUTO) {
    layoutType = AUTO_LAYOUT_TYPE;
  } else if (singlePageUncutBtn->isChecked()) {
    layoutType = SINGLE_PAGE_UNCUT;
  } else if (pagePlusOffcutBtn->isChecked()) {
    layoutType = PAGE_PLUS_OFFCUT;
  } else {
    layoutType = TWO_PAGES;
  }
  DefaultParams::PageSplitParams pageSplitParams(layoutType);

  DefaultParams::DeskewParams deskewParams(angleSpinBox->value(), deskewAutoBtn->isChecked() ? MODE_AUTO : MODE_MANUAL);

  const AutoManualMode pageBoxMode
      = pageDetectDisableBtn->isChecked() ? MODE_DISABLED : pageDetectManualBtn->isChecked() ? MODE_MANUAL : MODE_AUTO;
  DefaultParams::SelectContentParams selectContentParams(QSizeF(widthSpinBox->value(), heightSpinBox->value()),
                                                         !contentDetectDisableBtn->isChecked(), pageBoxMode,
                                                         fineTuneBtn->isChecked());

  Alignment alignment;
  switch (alignmentMode->currentIndex()) {
    case 0:
      if (autoVerticalAligningCB->isChecked()) {
        alignment.setVertical(Alignment::VAUTO);
      } else {
        alignment.setVertical(m_alignmentByButton.at(getCheckedAlignmentButton()).vertical());
      }
      if (autoHorizontalAligningCB->isChecked()) {
        alignment.setHorizontal(Alignment::HAUTO);
      } else {
        alignment.setHorizontal(m_alignmentByButton.at(getCheckedAlignmentButton()).horizontal());
      }
      break;
    case 1:
      alignment = m_alignmentByButton.at(getCheckedAlignmentButton());
      break;
    case 2:
      if (autoVerticalAligningCB->isChecked()) {
        alignment.setVertical(Alignment::VORIGINAL);
      } else {
        alignment.setVertical(m_alignmentByButton.at(getCheckedAlignmentButton()).vertical());
      }
      if (autoHorizontalAligningCB->isChecked()) {
        alignment.setHorizontal(Alignment::HORIGINAL);
      } else {
        alignment.setHorizontal(m_alignmentByButton.at(getCheckedAlignmentButton()).horizontal());
      }
      break;
    default:
      break;
  }
  alignment.setNull(!alignWithOthersCB->isChecked());
  DefaultParams::PageLayoutParams pageLayoutParams(Margins(leftMarginSpinBox->value(), topMarginSpinBox->value(),
                                                           rightMarginSpinBox->value(), bottomMarginSpinBox->value()),
                                                   alignment, autoMargins->isChecked());

  const int dpi = (dpiSelector->currentIndex() != m_customDpiItemIdx) ? dpiSelector->currentText().toInt()
                                                                      : m_customDpiValue.toInt();
  ColorParams colorParams;
  colorParams.setColorMode(static_cast<ColorMode>(colorModeSelector->currentData().toInt()));

  ColorCommonOptions colorCommonOptions;
  colorCommonOptions.setFillingColor(static_cast<FillingColor>(fillingColorBox->currentData().toInt()));
  colorCommonOptions.setFillMargins(fillMarginsCB->isChecked());
  colorCommonOptions.setFillOffcut(fillOffcutCB->isChecked());
  colorCommonOptions.setNormalizeIllumination(equalizeIlluminationColorCB->isChecked());
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setEnabled(posterizeCB->isChecked());
  posterizationOptions.setLevel(posterizeLevelSB->value());
  posterizationOptions.setNormalizationEnabled(posterizeNormalizationCB->isChecked());
  posterizationOptions.setForceBlackAndWhite(posterizeForceBwCB->isChecked());
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  colorParams.setColorCommonOptions(colorCommonOptions);

  BlackWhiteOptions blackWhiteOptions;
  blackWhiteOptions.setNormalizeIllumination(equalizeIlluminationCB->isChecked());
  blackWhiteOptions.setSavitzkyGolaySmoothingEnabled(savitzkyGolaySmoothingCB->isChecked());
  blackWhiteOptions.setMorphologicalSmoothingEnabled(morphologicalSmoothingCB->isChecked());
  BinarizationMethod binarizationMethod = static_cast<BinarizationMethod>(thresholdMethodBox->currentData().toInt());
  blackWhiteOptions.setBinarizationMethod(binarizationMethod);
  blackWhiteOptions.setThresholdAdjustment(thresholdSlider->value());
  blackWhiteOptions.setSauvolaCoef(sauvolaCoef->value());
  if (binarizationMethod == SAUVOLA) {
    blackWhiteOptions.setWindowSize(sauvolaWindowSize->value());
  } else if (binarizationMethod == WOLF) {
    blackWhiteOptions.setWindowSize(wolfWindowSize->value());
  }
  blackWhiteOptions.setWolfCoef(wolfCoef->value());
  blackWhiteOptions.setWolfLowerBound(upperBound->value());
  blackWhiteOptions.setWolfLowerBound(lowerBound->value());
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setEnabled(colorSegmentationCB->isChecked());
  segmenterOptions.setNoiseReduction(reduceNoiseSB->value());
  segmenterOptions.setRedThresholdAdjustment(redAdjustmentSB->value());
  segmenterOptions.setGreenThresholdAdjustment(greenAdjustmentSB->value());
  segmenterOptions.setBlueThresholdAdjustment(blueAdjustmentSB->value());
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  colorParams.setBlackWhiteOptions(blackWhiteOptions);

  SplittingOptions splittingOptions;
  splittingOptions.setSplitOutput(splittingCB->isChecked());
  splittingOptions.setSplittingMode(bwForegroundRB->isChecked() ? BLACK_AND_WHITE_FOREGROUND : COLOR_FOREGROUND);
  splittingOptions.setOriginalBackgroundEnabled(originalBackgroundCB->isChecked());

  PictureShapeOptions pictureShapeOptions;
  pictureShapeOptions.setPictureShape(static_cast<PictureShape>(pictureShapeSelector->currentData().toInt()));
  pictureShapeOptions.setSensitivity(pictureShapeSensitivitySB->value());
  pictureShapeOptions.setHigherSearchSensitivity(higherSearchSensitivityCB->isChecked());

  DewarpingOptions dewarpingOptions;
  dewarpingOptions.setDewarpingMode(static_cast<DewarpingMode>(dewarpingModeCB->currentData().toInt()));
  dewarpingOptions.setPostDeskew(dewarpingPostDeskewCB->isChecked());

  double despeckleLevel;
  if (despeckleCB->isChecked()) {
    despeckleLevel = 0.1 * despeckleSlider->value();
  } else {
    despeckleLevel = 0;
  }

  DefaultParams::OutputParams outputParams(Dpi(dpi, dpi), colorParams, splittingOptions, pictureShapeOptions,
                                           DepthPerception(0.1 * depthPerceptionSlider->value()), dewarpingOptions,
                                           despeckleLevel);

  std::unique_ptr<DefaultParams> defaultParams = std::make_unique<DefaultParams>(
      fixOrientationParams, deskewParams, pageSplitParams, selectContentParams, pageLayoutParams, outputParams);
  defaultParams->setUnits(m_currentUnits);

  return defaultParams;
}

void DefaultParamsDialog::updateUnits(const Units units) {
  m_currentUnits = units;
  unitsLabel->setText(unitsToLocalizedString(units));

  {
    int decimals;
    double step;
    switch (units) {
      case PIXELS:
      case MILLIMETRES:
        decimals = 1;
        step = 1.0;
        break;
      default:
        decimals = 2;
        step = 0.01;
        break;
    }

    widthSpinBox->setDecimals(decimals);
    widthSpinBox->setSingleStep(step);
    heightSpinBox->setDecimals(decimals);
    heightSpinBox->setSingleStep(step);
  }

  {
    int decimals;
    double step;
    switch (units) {
      case PIXELS:
      case MILLIMETRES:
        decimals = 1;
        step = 1.0;
        break;
      default:
        decimals = 2;
        step = 0.01;
        break;
    }

    topMarginSpinBox->setDecimals(decimals);
    topMarginSpinBox->setSingleStep(step);
    bottomMarginSpinBox->setDecimals(decimals);
    bottomMarginSpinBox->setSingleStep(step);
    leftMarginSpinBox->setDecimals(decimals);
    leftMarginSpinBox->setSingleStep(step);
    rightMarginSpinBox->setDecimals(decimals);
    rightMarginSpinBox->setSingleStep(step);
  }
}

void DefaultParamsDialog::setLinkButtonLinked(QToolButton* button, bool linked) {
  button->setIcon(linked ? m_chainIcon : m_brokenChainIcon);
}

void DefaultParamsDialog::topBottomLinkClicked() {
  m_topBottomLinkEnabled = !m_topBottomLinkEnabled;
  setLinkButtonLinked(topBottomLink, m_topBottomLinkEnabled);
  if (m_topBottomLinkEnabled && (topMarginSpinBox->value() != bottomMarginSpinBox->value())) {
    const double new_margin = std::min(topMarginSpinBox->value(), bottomMarginSpinBox->value());
    topMarginSpinBox->setValue(new_margin);
    bottomMarginSpinBox->setValue(new_margin);
  }
}

void DefaultParamsDialog::leftRightLinkClicked() {
  m_leftRightLinkEnabled = !m_leftRightLinkEnabled;
  setLinkButtonLinked(leftRightLink, m_leftRightLinkEnabled);
  if (m_leftRightLinkEnabled && (leftMarginSpinBox->value() != rightMarginSpinBox->value())) {
    const double new_margin = std::min(leftMarginSpinBox->value(), rightMarginSpinBox->value());
    leftMarginSpinBox->setValue(new_margin);
    rightMarginSpinBox->setValue(new_margin);
  }
}

void DefaultParamsDialog::horMarginsChanged(double val) {
  if (m_ignoreMarginChanges) {
    return;
  }
  if (m_leftRightLinkEnabled) {
    const ScopedIncDec<int> scopeGuard(m_ignoreMarginChanges);
    leftMarginSpinBox->setValue(val);
    rightMarginSpinBox->setValue(val);
  }
}

void DefaultParamsDialog::vertMarginsChanged(double val) {
  if (m_ignoreMarginChanges) {
    return;
  }
  if (m_topBottomLinkEnabled) {
    const ScopedIncDec<int> scopeGuard(m_ignoreMarginChanges);
    topMarginSpinBox->setValue(val);
    bottomMarginSpinBox->setValue(val);
  }
}

void DefaultParamsDialog::thresholdSliderValueChanged(int value) {
  const QString tooltip_text(QString::number(value));
  thresholdSlider->setToolTip(tooltip_text);

  thresholLabel->setText(QString::number(value));

  // Show the tooltip immediately.
  const QPoint center(thresholdSlider->rect().center());
  QPoint tooltip_pos(thresholdSlider->mapFromGlobal(QCursor::pos()));
  tooltip_pos.setY(center.y());
  tooltip_pos.setX(qBound(0, tooltip_pos.x(), thresholdSlider->width()));
  tooltip_pos = thresholdSlider->mapToGlobal(tooltip_pos);
  QToolTip::showText(tooltip_pos, tooltip_text, thresholdSlider);
}

void DefaultParamsDialog::setLighterThreshold() {
  thresholdSlider->setValue(thresholdSlider->value() - 1);
  thresholLabel->setText(QString::number(thresholdSlider->value()));
}

void DefaultParamsDialog::setDarkerThreshold() {
  thresholdSlider->setValue(thresholdSlider->value() + 1);
  thresholLabel->setText(QString::number(thresholdSlider->value()));
}

void DefaultParamsDialog::setNeutralThreshold() {
  thresholdSlider->setValue(0);
  thresholLabel->setText(QString::number(thresholdSlider->value()));
}

void DefaultParamsDialog::dpiSelectionChanged(int index) {
  dpiSelector->setEditable(index == m_customDpiItemIdx);
  if (index == m_customDpiItemIdx) {
    dpiSelector->setEditText(m_customDpiValue);
    dpiSelector->lineEdit()->selectAll();
    // It looks like we need to set a new validator
    // every time we make the combo box editable.
    dpiSelector->setValidator(new QIntValidator(0, 9999, dpiSelector));
  }
}

void DefaultParamsDialog::dpiEditTextChanged(const QString& text) {
  if (dpiSelector->currentIndex() == m_customDpiItemIdx) {
    m_customDpiValue = text;
  }
}

void DefaultParamsDialog::depthPerceptionChangedSlot(const int val) {
  const QString tooltip_text(QString::number(0.1 * val));
  depthPerceptionSlider->setToolTip(tooltip_text);

  // Show the tooltip immediately.
  const QPoint center(depthPerceptionSlider->rect().center());
  QPoint tooltip_pos(depthPerceptionSlider->mapFromGlobal(QCursor::pos()));
  tooltip_pos.setY(center.y());
  tooltip_pos.setX(qBound(0, tooltip_pos.x(), depthPerceptionSlider->width()));
  tooltip_pos = depthPerceptionSlider->mapToGlobal(tooltip_pos);
  QToolTip::showText(tooltip_pos, tooltip_text, depthPerceptionSlider);
}

void DefaultParamsDialog::profileChanged(const int index) {
  if (m_ignoreProfileChanges) {
    return;
  }

  profileCB->setEditable(index == m_customProfileItemIdx);
  if (index == m_customProfileItemIdx) {
    profileCB->setEditText(profileCB->currentText());
    profileCB->lineEdit()->selectAll();
    profileCB->setFocus();
    profileSaveButton->setEnabled(true);
    profileDeleteButton->setEnabled(false);
    setTabWidgetsEnabled(true);

    if (DefaultParamsProvider::getInstance()->getProfileName() == "Custom") {
      loadParams(DefaultParamsProvider::getInstance()->getParams());
    } else {
      updateUnits(UnitsProvider::getInstance()->getUnits());
    }
  } else if (profileCB->currentData().toString() == "Default") {
    profileSaveButton->setEnabled(false);
    profileDeleteButton->setEnabled(false);
    setTabWidgetsEnabled(false);

    std::unique_ptr<DefaultParams> defaultProfile = m_profileManager.createDefaultProfile();
    loadParams(*defaultProfile);
  } else if (profileCB->currentData().toString() == "Source") {
    profileSaveButton->setEnabled(false);
    profileDeleteButton->setEnabled(false);
    setTabWidgetsEnabled(false);

    std::unique_ptr<DefaultParams> sourceProfile = m_profileManager.createSourceProfile();
    loadParams(*sourceProfile);
  } else {
    const QString profileName = profileCB->currentData().toString();
    DefaultParamsProfileManager::LoadStatus loadStatus;

    std::unique_ptr<DefaultParams> profile = m_profileManager.readProfile(profileName, &loadStatus);
    if (loadStatus == DefaultParamsProfileManager::SUCCESS) {
      profileSaveButton->setEnabled(true);
      profileDeleteButton->setEnabled(true);
      setTabWidgetsEnabled(true);

      loadParams(*profile);
    } else {
      if (loadStatus == DefaultParamsProfileManager::INCOMPATIBLE_VERSION_ERROR) {
        const int result = QMessageBox::warning(
            this, tr("Error"), tr("The profile file is not compatible with the current application version. Remove?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (result == QMessageBox::Yes) {
          m_profileManager.deleteProfile(profileName);
        }
      } else {
        QMessageBox::critical(this, tr("Error"), tr("Error loading the profile."));
      }
      profileCB->setCurrentIndex(0);
      profileCB->removeItem(index);
      m_customProfileItemIdx--;

      profileSaveButton->setEnabled(false);
      profileDeleteButton->setEnabled(false);
      setTabWidgetsEnabled(false);

      std::unique_ptr<DefaultParams> defaultProfile = m_profileManager.createDefaultProfile();
      loadParams(*defaultProfile);
    }
  }
}

void DefaultParamsDialog::profileSavePressed() {
  const ScopedIncDec<int> scopeGuard(m_ignoreProfileChanges);

  if (isProfileNameReserved(profileCB->currentText())) {
    QMessageBox::information(this, tr("Error"),
                             tr("The name conflicts with a default profile name. Please enter a different name."));
    return;
  }

  if (m_profileManager.writeProfile(*buildParams(), profileCB->currentText())) {
    if (profileCB->currentIndex() == m_customProfileItemIdx) {
      const QString profileName = profileCB->currentText();
      profileCB->setItemData(profileCB->currentIndex(), profileName);
      profileCB->setItemText(profileCB->currentIndex(), profileName);
      m_customProfileItemIdx = profileCB->count();
      profileCB->addItem(tr("Custom"), "Custom");

      profileCB->setEditable(false);
      profileDeleteButton->setEnabled(true);
    }
  } else {
    QMessageBox::critical(this, tr("Error"), tr("Error saving the profile."));
  }
}

void DefaultParamsDialog::profileDeletePressed() {
  if (m_profileManager.deleteProfile(profileCB->currentText())) {
    {
      const ScopedIncDec<int> scopeGuard(m_ignoreProfileChanges);

      const int deletedProfileIndex = profileCB->currentIndex();
      profileCB->setCurrentIndex(m_customProfileItemIdx--);
      profileCB->removeItem(deletedProfileIndex);
    }
    profileChanged(m_customProfileItemIdx);
  } else {
    QMessageBox::critical(this, tr("Error"), tr("Error deleting the profile."));
  }
}

bool DefaultParamsDialog::isProfileNameReserved(const QString& name) {
  return m_reservedProfileNames.find(name) != m_reservedProfileNames.end();
}

void DefaultParamsDialog::commitChanges() {
  const QString profile = profileCB->currentData().toString();
  std::unique_ptr<DefaultParams> params;
  if (profile == "Default") {
    params = m_profileManager.createDefaultProfile();
  } else if (profile == "Source") {
    params = m_profileManager.createSourceProfile();
  } else {
    params = buildParams();
  }
  DefaultParamsProvider::getInstance()->setParams(std::move(params), profile);
  QSettings().setValue("settings/current_profile", profile);
}

void DefaultParamsDialog::setTabWidgetsEnabled(const bool enabled) {
  for (int i = 0; i < tabWidget->count(); i++) {
    QWidget* widget = tabWidget->widget(i);
    widget->setEnabled(enabled);
  }
}

void DefaultParamsDialog::colorSegmentationToggled(bool checked) {
  segmenterOptionsWidget->setEnabled(checked);
  if ((colorModeSelector->currentData() == BLACK_AND_WHITE) || (colorModeSelector->currentData() == MIXED)) {
    posterizeCB->setEnabled(checked);
    posterizeOptionsWidget->setEnabled(checked && posterizeCB->isChecked());
  }
}

void DefaultParamsDialog::posterizeToggled(bool checked) {
  posterizeOptionsWidget->setEnabled(checked);
}

void DefaultParamsDialog::updateAlignmentButtonsEnabled() {
  bool enableHorizontalButtons;
  bool enableVerticalButtons;
  if ((alignmentMode->currentIndex() == 0) || (alignmentMode->currentIndex() == 2)) {
    enableHorizontalButtons = !autoHorizontalAligningCB->isChecked() ? alignWithOthersCB->isChecked() : false;
    enableVerticalButtons = !autoVerticalAligningCB->isChecked() ? alignWithOthersCB->isChecked() : false;
  } else {
    enableHorizontalButtons = enableVerticalButtons = alignWithOthersCB->isChecked();
  }

  alignTopLeftBtn->setEnabled(enableHorizontalButtons && enableVerticalButtons);
  alignTopBtn->setEnabled(enableVerticalButtons);
  alignTopRightBtn->setEnabled(enableHorizontalButtons && enableVerticalButtons);
  alignLeftBtn->setEnabled(enableHorizontalButtons);
  alignCenterBtn->setEnabled(enableHorizontalButtons || enableVerticalButtons);
  alignRightBtn->setEnabled(enableHorizontalButtons);
  alignBottomLeftBtn->setEnabled(enableHorizontalButtons && enableVerticalButtons);
  alignBottomBtn->setEnabled(enableVerticalButtons);
  alignBottomRightBtn->setEnabled(enableHorizontalButtons && enableVerticalButtons);
}

void DefaultParamsDialog::updateAutoModeButtons() {
  if ((alignmentMode->currentIndex() == 0) || (alignmentMode->currentIndex() == 2)) {
    if (autoVerticalAligningCB->isChecked() && !autoHorizontalAligningCB->isChecked()) {
      autoVerticalAligningCB->setEnabled(false);
    } else if (autoHorizontalAligningCB->isChecked() && !autoVerticalAligningCB->isChecked()) {
      autoHorizontalAligningCB->setEnabled(false);
    } else {
      autoVerticalAligningCB->setEnabled(true);
      autoHorizontalAligningCB->setEnabled(true);
    }
  }

  if (autoVerticalAligningCB->isChecked() && !autoHorizontalAligningCB->isChecked()) {
    switch (m_alignmentByButton.at(getCheckedAlignmentButton()).horizontal()) {
      case Alignment::LEFT:
        alignLeftBtn->setChecked(true);
        break;
      case Alignment::RIGHT:
        alignRightBtn->setChecked(true);
        break;
      default:
        alignCenterBtn->setChecked(true);
        break;
    }
  } else if (autoHorizontalAligningCB->isChecked() && !autoVerticalAligningCB->isChecked()) {
    switch (m_alignmentByButton.at(getCheckedAlignmentButton()).vertical()) {
      case Alignment::TOP:
        alignTopBtn->setChecked(true);
        break;
      case Alignment::BOTTOM:
        alignBottomBtn->setChecked(true);
        break;
      default:
        alignCenterBtn->setChecked(true);
        break;
    }
  }
}

QToolButton* DefaultParamsDialog::getCheckedAlignmentButton() const {
  auto* checkedButton = dynamic_cast<QToolButton*>(m_alignmentButtonGroup->checkedButton());
  if (!checkedButton) {
    checkedButton = alignCenterBtn;
  }

  return checkedButton;
}

void DefaultParamsDialog::autoHorizontalAligningToggled(bool) {
  updateAlignmentButtonsEnabled();
  updateAutoModeButtons();
}

void DefaultParamsDialog::autoVerticalAligningToggled(bool) {
  updateAlignmentButtonsEnabled();
  updateAutoModeButtons();
}

void DefaultParamsDialog::despeckleToggled(bool checked) {
  despeckleSlider->setEnabled(checked);
}

void DefaultParamsDialog::despeckleSliderValueChanged(int value) {
  const double new_value = 0.1 * value;

  const QString tooltip_text(QString::number(new_value));
  despeckleSlider->setToolTip(tooltip_text);

  // Show the tooltip immediately.
  const QPoint center(despeckleSlider->rect().center());
  QPoint tooltip_pos(despeckleSlider->mapFromGlobal(QCursor::pos()));
  tooltip_pos.setY(center.y());
  tooltip_pos.setX(qBound(0, tooltip_pos.x(), despeckleSlider->width()));
  tooltip_pos = despeckleSlider->mapToGlobal(tooltip_pos);
  QToolTip::showText(tooltip_pos, tooltip_text, despeckleSlider);
}

void DefaultParamsDialog::updateAlignmentModeEnabled() {
  const bool is_alignment_null = !alignWithOthersCB->isChecked();

  alignmentMode->setEnabled(!is_alignment_null);
  autoAlignSettingsGroup->setEnabled(!is_alignment_null);
}
