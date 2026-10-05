// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies

#include "testing/environment.h"
#include "draw/drawmodule.h"
#include "engraving/engravingmodule.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/mscore.h"
#include "engraving/tests/utils/scorerw.h"
#include "engraving/tests/mocks/engravingconfigurationmock.h"

static const mu::engraving::IEngravingConfiguration::DebuggingOptions debuggingOptions {};

static muse::testing::SuiteEnvironment widgetEnvironment
    = muse::testing::SuiteEnvironment()
      .setDependencyModules({ new muse::draw::DrawModule(), new mu::engraving::EngravingModule() })
      .setPostInit([]() {
    mu::engraving::ScoreRW::setRootPath(muse::String::fromUtf8(notationscene_widget_tests_DATA_ROOT));
    mu::engraving::MScore::testMode = true;
    mu::engraving::MScore::testWriteStyleToScore = false;
    mu::engraving::MScore::noGui = true;
    mu::engraving::loadInstrumentTemplates(":/engraving/instruments/instruments.xml");

    // Match the engraving suite: rendering must have a deterministic black
    // default and avoid accessibility services that are absent in this runner.
    using ConfigurationMock = testing::NiceMock<mu::engraving::EngravingConfigurationMock>;
    std::shared_ptr<ConfigurationMock> configuration(new ConfigurationMock(), [](ConfigurationMock*) {});
    ON_CALL(*configuration, isAccessibleEnabled()).WillByDefault(testing::Return(false));
    ON_CALL(*configuration, defaultColor()).WillByDefault(testing::Return(muse::draw::Color::BLACK));
    ON_CALL(*configuration, debuggingOptions()).WillByDefault(testing::ReturnRef(debuggingOptions));
    ON_CALL(*configuration, allowReadingImagesFromOutsideMscz()).WillByDefault(testing::Return(true));
    muse::modularity::globalIoc()->unregister<mu::engraving::IEngravingConfiguration>("widget-tests");
    muse::modularity::globalIoc()->registerExport<mu::engraving::IEngravingConfiguration>("widget-tests", configuration);
}).setDeInit([]() {
    // Like the engraving suite, explicitly release the mock after all tests:
    // static engraving objects can retain injected shared_ptrs past this point.
    auto configuration = muse::modularity::globalIoc()->resolve<mu::engraving::IEngravingConfiguration>("widget-tests");
    muse::modularity::globalIoc()->unregister<mu::engraving::IEngravingConfiguration>("widget-tests");
    delete configuration.get();
});
