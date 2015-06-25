#include "DataSource.h"
#include "DataGrid.h"
#include "CoordinateSystems.h"
#include "ImageGridServiceSynchronizer.h"
#include "Data/Colormap/Colormaps.h"
#include "Globals.h"
#include "PluginManager.h"
#include "GrayColormap.h"
#include "CartaLib/IImage.h"
#include "Data/Util.h"
#include "Data/Colormap/TransformsData.h"
#include "CartaLib/Hooks/GetWcsGridRenderer.h"
#include "CartaLib/Hooks/LoadAstroImage.h"

#include "CartaLib/PixelPipeline/CustomizablePixelPipeline.h"
#include "../../ImageRenderService.h"
#include "../../ScriptedRenderService.h"
#include "../../Algorithms/quantileAlgorithms.h"
#include <QDebug>
#include <QDir>
#include <QImageWriter>
#include <QTime>

namespace Carta {

namespace Data {

const QString DataSource::DATA_PATH = "file";
const QString DataSource::CLASS_NAME = "DataSource";

CoordinateSystems* DataSource::m_coords = nullptr;

class DataSource::Factory : public Carta::State::CartaObjectFactory {

public:

    Carta::State::CartaObject * create (const QString & path, const QString & id)
    {
        return new DataSource (path, id);
    }
};
bool DataSource::m_registered =
        Carta::State::ObjectManager::objectManager()->registerClass (CLASS_NAME,
                                                   new DataSource::Factory());



DataSource::DataSource(const QString& path, const QString& id) :
        CartaObject( CLASS_NAME, path, id),
    m_image( nullptr ),
    m_wcsGridRenderer( nullptr ),
    m_igSync( nullptr )
    {
        m_cmapUseCaching = true;
        m_cmapUseInterpolatedCaching = true;
        m_cmapCacheSize = 1000;

        _initializeSingletons();

        //Initialize the rendering service
        m_renderService.reset( new Carta::Core::ImageRenderService::Service() );

        Carta::State::CartaObject* gridObj = Util::createObject( DataGrid::CLASS_NAME );
        m_dataGrid.reset( dynamic_cast<DataGrid*>(gridObj) );
        m_dataGrid->_initializeGridRenderer();
        _initializeState();

        m_wcsGridRenderer = m_dataGrid->_getRenderer();

        // create the synchronizer
        m_igSync.reset( new ImageGridServiceSynchronizer( m_renderService, m_wcsGridRenderer, this ) );

        // connect its done() slot to our imageAndGridDoneSlot()
        connect( m_igSync.get(), & ImageGridServiceSynchronizer::done,
                 this, & DataSource::_imageAndGridDoneSlot );

        // assign a default colormap to the view
        auto rawCmap = std::make_shared < Carta::Core::GrayColormap > ();

        // initialize pixel pipeline
        m_pixelPipeline = std::make_shared < Carta::Lib::PixelPipeline::CustomizablePixelPipeline > ();
        m_pixelPipeline-> setInvert( false );
        m_pixelPipeline-> setReverse( false );
        m_pixelPipeline-> setColormap( std::make_shared < Carta::Core::GrayColormap > () );
        m_pixelPipeline-> setMinMax( 0, 1 );
        m_renderService-> setPixelPipeline( m_pixelPipeline, m_pixelPipeline-> cacheId());
}

bool DataSource::_contains(const QString& fileName) const {
    bool representsData = false;
    QString imageName = m_state.getValue<QString>(DATA_PATH);
    if (imageName.endsWith(fileName)) {
        representsData = true;
    }
    return representsData;
}

QString DataSource::_getCursorText( int mouseX, int mouseY, int frameIndex){
    QString str;
    QTextStream out( & str );
    QPointF lastMouse( mouseX, mouseY );

    bool valid = false;
    QPointF imgPt = _getImagePt( lastMouse, &valid );
    if ( valid ){
        double imgX = imgPt.x();
        double imgY = imgPt.y();
    
        CoordinateFormatterInterface::SharedPtr cf(
                m_image-> metaData()-> coordinateFormatter()-> clone() );


        QString coordName = m_coords->getName( cf->skyCS() );
        out << "Default sky cs:" << coordName << "\n";
        out << "Image cursor:" << imgX << "," << imgY << "\n";
        QString pixelValue = _getPixelValue( imgX, imgY );
        out << "Value:" << pixelValue << " " << m_image->getPixelUnit().toStr() << "\n";
    
        QList<Carta::Lib::KnownSkyCS> css = m_coords->getIndices();
        for ( Carta::Lib::KnownSkyCS cs : css ) {
            cf-> setSkyCS( cs );
            out << m_coords->getName( cs ) << ": ";
            std::vector < Carta::Lib::AxisInfo > ais;
            for ( int axis = 0 ; axis < cf->nAxes() ; axis++ ) {
                const Carta::Lib::AxisInfo & ai = cf-> axisInfo( axis );
                ais.push_back( ai );
            }
            std::vector < double > pixel( m_image-> dims().size(), 0.0 );
            pixel[0] = imgX;
            pixel[1] = imgY;
            if( pixel.size() > 2) {
                pixel[2] = frameIndex;
            }
            auto list = cf-> formatFromPixelCoordinate( pixel );
            for ( size_t i = 0 ; i < ais.size() ; i++ ) {
                out << ais[i].shortLabel().html() << ":" << list[i] << " ";
            }
            out << "\n";
        }

        str.replace( "\n", "<br />" );
    }
    return str;
}

QPointF DataSource::_getCenter() const{
    return m_renderService->pan();
}


Carta::State::StateInterface DataSource::_getGridState() const {
    return m_dataGrid->_getState();
}

QPointF DataSource::_getImagePt( QPointF screenPt, bool* valid ) const {
    QPointF imagePt;
    if ( m_image != nullptr ){
        imagePt = m_renderService-> screen2img (screenPt);
        *valid = true;
    }
    else {
        *valid = false;
    }
    return imagePt;
}

QString DataSource::_getPixelValue( double x, double y ){
    QString pixelValue = "";
    if ( x >= 0 && x < m_image->dims()[0] && y >= 0 && y < m_image->dims()[1] ) {
        NdArray::RawViewInterface* rawData = _getRawData( 0, 0 );
        if ( rawData != nullptr ){
            NdArray::TypedView<double> view( rawData, false );
            pixelValue = QString::number( view.get( {(int)(x), int(y)} ) );
        }
    }
    return pixelValue;
}


QPointF DataSource::_getScreenPt( QPointF imagePt, bool* valid ) const {
    QPointF screenPt;
    if ( m_image != nullptr ){
        screenPt = m_renderService->img2screen( imagePt );
        *valid = true;
    }
    else {
        *valid = false;
    }
    return screenPt;
}

int DataSource::_getFrameCount() const {
    int frameCount = 1;
    if ( m_image ){
        std::vector<int> imageShape  = m_image->dims();
        if ( imageShape.size() > 2 ){
            frameCount = imageShape[2];
        }
    }
    return frameCount;
}



int DataSource::_getDimension( int coordIndex ) const {
    int dim = -1;
    if ( 0 <= coordIndex && coordIndex < _getDimensions()){
        dim = m_image-> dims()[coordIndex];
    }
    return dim;
}


int DataSource::_getDimensions() const {
    int imageSize = 0;
    if ( m_image ){
        imageSize = m_image->dims().size();
    }
    return imageSize;
}

QString DataSource::_getFileName() const {
    return m_state.getValue<QString>(DATA_PATH);
}

std::shared_ptr<Image::ImageInterface> DataSource::_getImage(){
    return m_image;
}



std::shared_ptr<Carta::Lib::PixelPipeline::CustomizablePixelPipeline> DataSource::_getPipeline() const {
    return m_pixelPipeline;

}

bool DataSource::_getIntensity( int frameLow, int frameHigh, double percentile, double* intensity ) const {
    bool intensityFound = false;
    NdArray::RawViewInterface* rawData = _getRawData( frameLow, frameHigh );
    if ( rawData != nullptr ){
        NdArray::TypedView<double> view( rawData, false );
        // read in all values from the view into an array
        // we need our own copy because we'll do quickselect on it...
        std::vector < double > allValues;
        view.forEach(
                [& allValues] ( const double  val ) {
            if ( std::isfinite( val ) ) {
                allValues.push_back( val );
            }
        }
        );

        // indicate bad clip if no finite numbers were found
        if ( allValues.size() > 0 ) {
            int locationIndex = allValues.size() * percentile - 1;

            if ( locationIndex < 0 ){
                locationIndex = 0;
            }
            std::nth_element( allValues.begin(), allValues.begin()+locationIndex, allValues.end() );
            *intensity = allValues[locationIndex];
            intensityFound = true;
        }
    }
    return intensityFound;
}

double DataSource::_getPercentile( int frameLow, int frameHigh, double intensity ) const {
    double percentile = 0;
    NdArray::RawViewInterface* rawData = _getRawData( frameLow, frameHigh );
    if ( rawData != nullptr ){
        u_int64_t totalCount = 0;
        u_int64_t countBelow = 0;
        NdArray::TypedView<double> view( rawData, false );
        view.forEach([&](const double& val) {
            if( Q_UNLIKELY( std::isnan(val))){
                return;
            }
            totalCount ++;
            if( val <= intensity){
                countBelow++;
            }
            return;
        });

        if ( totalCount > 0 ){
            percentile = double(countBelow) / totalCount;
        }
    }
    return percentile;
}

QStringList DataSource::_getPixelCoordinates( double ra, double dec ){
    QStringList result("");
    CoordinateFormatterInterface::SharedPtr cf( m_image-> metaData()-> coordinateFormatter()-> clone() );
    const CoordinateFormatterInterface::VD world { ra, dec };
    CoordinateFormatterInterface::VD pixel;
    bool valid = cf->toPixel( world, pixel );
    if ( valid ){
        result = QStringList( QString::number( pixel[0] ) );
        result.append( QString::number( pixel[1] ) );
    }
    return result;
}

NdArray::RawViewInterface * DataSource::_getRawData( int channelStart, int channelEnd ) const {
    NdArray::RawViewInterface* rawData = nullptr;
    if ( m_image ){
        auto frameSlice = SliceND().next();
        for( size_t i=2; i < m_image->dims().size(); i++ ){
            if ( i == 2 ){
                SliceND& slice = frameSlice.next();
                if (channelStart>=0 && channelEnd >= 0 ){
                    slice.start( channelStart );
                    slice.end( channelEnd + 1);
                    
                 }
                 else {
                    slice.start( 0 );
                    slice.end( m_image->dims()[2] );
                 }
                 slice.step( 1 );
            }
            else {
                frameSlice.next().index(0);
            }
        }
        rawData = m_image->getDataSlice( frameSlice );
    }
    return rawData;
}

QString DataSource::_getStateString() const{
    return m_state.toString();
}

double DataSource::_getZoom() const {
    return m_renderService-> zoom();
}

QSize DataSource::_getOutputSize() const {
    return m_renderService-> outputSize();
}

void DataSource::_gridChanged( const Carta::State::StateInterface& state, bool renderImage ){
    bool stateChanged = m_dataGrid->_resetState( state );
    if ( stateChanged ){
        m_state.setObject(DataGrid::GRID, m_dataGrid->_getState().toString());
        if ( renderImage ){
            _render();
        }
    }
}

void DataSource::_imageAndGridDoneSlot(
        QImage image,
        Carta::Lib::VectorGraphics::VGList vgList,
        int64_t /*jobId*/){
    /// \todo we should make sure the jobId matches the last submitted job...
    //qDebug() << "Image and grid done slot";

    m_qimage = image;
    // draw the grid over top
    QTime t;
    t.restart();
    QPainter painter( & m_qimage );
    painter.setRenderHint( QPainter::Antialiasing, true );
    Carta::Lib::VectorGraphics::VGListQPainterRenderer vgRenderer;
    if ( ! vgRenderer.render( vgList, painter ) ) {
        qWarning() << "could not render grid vector graphics";
    }

    // schedule a repaint with the connector
    emit renderingDone( m_qimage );

}

void DataSource::_initializeSingletons( ){
    //Load the available color maps.
    if ( m_coords == nullptr ){
        Carta::State::CartaObject* obj = Util::findSingletonObject( CoordinateSystems::CLASS_NAME );
        m_coords = dynamic_cast<CoordinateSystems*>( obj );
    }
}

void DataSource::_initializeState(){
    m_state.insertValue<QString>(DATA_PATH, "");
    m_state.insertObject( DataGrid::GRID, m_dataGrid->_getState().toString());
    //Nobody is listening on the client side so we don't need to flush the state.
}

void DataSource::_load(int frameIndex, bool /*recomputeClipsOnNewFrame*/, double minClipPercentile, double maxClipPercentile){

    if ( frameIndex < 0 ) {
        frameIndex = 0;
    }
    if ( m_image-> dims().size() <= 2 ) {
        frameIndex = 0;
    }
    else {
        frameIndex = Carta::Lib::clamp( frameIndex, 0, m_image-> dims()[2] - 1 );
    }

    // prepare slice description corresponding to the entire frame [:,:,frame,0,0,...0]
    auto frameSlice = SliceND().next();
    for ( size_t i = 2 ; i < m_image->dims().size() ; i++ ) {
        frameSlice.next().index( i == 2 ? frameIndex : 0 );
    }

    // get a view of the data using the slice description and make a shared pointer out of it
    NdArray::RawViewInterface::SharedPtr view( m_image-> getDataSlice( frameSlice ) );

    //Update the clip values
    _updateClips( view, frameIndex, minClipPercentile, maxClipPercentile );

    m_renderService-> setPixelPipeline( m_pixelPipeline, m_pixelPipeline-> cacheId());

    // tell the render service to render this job
    QString fileName = m_state.getValue<QString>(DATA_PATH);
    QString argStr = QString( "%1//%2").arg( fileName ).arg(frameIndex);
    m_renderService-> setInputView( view, argStr);
    // if grid is active, request a grid rendering as well

    if ( m_wcsGridRenderer ) {
        m_wcsGridRenderer-> setInputImage( m_image );
    }
    _render();

}

void DataSource::_render(){
    // erase current grid

    QSize renderSize = m_renderService-> outputSize();
    m_wcsGridRenderer-> setOutputSize( renderSize );

    int leftMargin = 50;
    int rightMargin = 10;
    int bottomMargin = 50;
    int topMargin = 10;

    QRectF outputRect( leftMargin, topMargin,
                       renderSize.width() - leftMargin - rightMargin,
                       renderSize.height() - topMargin - bottomMargin );

    QRectF inputRect(
        m_renderService-> screen2img( outputRect.topLeft() ),
        m_renderService-> screen2img( outputRect.bottomRight() ) );

    m_wcsGridRenderer-> setImageRect( inputRect );
    m_wcsGridRenderer-> setOutputRect( outputRect );

    m_igSync-> start();
}


void DataSource::_saveFullImage( const QString& savename, int width, int height, double scale,
        const Qt::AspectRatioMode aspectRatioMode ){
    QString fileName = _getFileName();
    m_scriptedRenderService = new Carta::Core::ScriptedClient::ScriptedRenderService( savename, m_image, m_pixelPipeline, fileName );
    if ( width > 0 && height > 0 ) {
        m_scriptedRenderService->setOutputSize( QSize( width, height ) );
        m_scriptedRenderService->setAspectRatioMode( aspectRatioMode );
    }
    m_scriptedRenderService->setZoom( scale );

    connect( m_scriptedRenderService, & Carta::Core::ScriptedClient::ScriptedRenderService::saveImageResult, this, & DataSource::_saveImageResultCB );

    m_scriptedRenderService->saveFullImage();
}

void DataSource::_saveImageResultCB( bool result ){
    emit saveImageResult( result );
    m_scriptedRenderService->deleteLater();
}

bool DataSource::_setFileName( const QString& fileName ){
    QString file = fileName.trimmed();
    bool successfulLoad = true;
    if (file.length() > 0) {
        if ( file != m_state.getValue<QString>(DATA_PATH)){
            try {
                auto res = Globals::instance()-> pluginManager()
                                      -> prepare <Carta::Lib::Hooks::LoadAstroImage>( file )
                                      .first();
                if (!res.isNull()){
                    m_image = res.val();

                    // reset zoom/pan
                    m_renderService-> setZoom( 1.0 );
                    m_renderService-> setPan( { m_image-> dims()[0] / 2.0, m_image-> dims()[1] / 2.0 }
                                              );

                    // clear quantile cache
                    m_quantileCache.resize(0);
                    int nf = 1;
                    if( m_image-> dims().size() > 2){
                        nf = m_image-> dims()[2];
                    }
                    m_quantileCache.resize( nf);
                    m_state.setValue<QString>( DATA_PATH, file );
                }
                else {
                    qWarning( "Could not find any plugin to load image");
                    successfulLoad = false;
                }

            }
            catch( std::logic_error& err ){
                qDebug() << "Failed to load image "<<fileName;
                successfulLoad = false;
            }
        }
    }
    else {
        qDebug() << "Cannot load empty file";
        successfulLoad = false;
    }
    return successfulLoad;
}

void DataSource::setColorMap( const QString& name ){
    Carta::State::ObjectManager* objManager = Carta::State::ObjectManager::objectManager();
    Carta::State::CartaObject* obj = objManager->getObject( Colormaps::CLASS_NAME );
    Colormaps* maps = dynamic_cast<Colormaps*>(obj);
    m_pixelPipeline-> setColormap( maps->getColorMap( name ) );
    m_renderService ->setPixelPipeline( m_pixelPipeline, m_pixelPipeline->cacheId());
}

void DataSource::setColorInverted( bool inverted ){
    m_pixelPipeline-> setInvert( inverted );
    m_renderService-> setPixelPipeline( m_pixelPipeline, m_pixelPipeline-> cacheId());
}

void DataSource::setColorReversed( bool reversed ){
    m_pixelPipeline-> setReverse( reversed );
    m_renderService-> setPixelPipeline( m_pixelPipeline, m_pixelPipeline-> cacheId());
}

void DataSource::setColorAmounts( double newRed, double newGreen, double newBlue ){
    std::array<double,3> colorArray;
    colorArray[0] = newRed;
    colorArray[1] = newGreen;
    colorArray[2] = newBlue;
    m_pixelPipeline->setRgbMax( colorArray );
    m_renderService->setPixelPipeline( m_pixelPipeline, m_pixelPipeline->cacheId());
}

void DataSource::_setPan( double imgX, double imgY ){
    m_renderService-> setPan( QPointF(imgX,imgY) );
}

void DataSource::_setTransformData( const QString& name ){
    Carta::State::CartaObject* transformDataObj = Util::findSingletonObject( TransformsData::CLASS_NAME );
    TransformsData* transformData = dynamic_cast<TransformsData*>(transformDataObj);
    Carta::Lib::PixelPipeline::ScaleType scaleType = transformData->getScaleType( name );
    m_pixelPipeline->setScale( scaleType );
    m_renderService->setPixelPipeline( m_pixelPipeline, m_pixelPipeline->cacheId() );
}

void DataSource::_setZoom( double zoomAmount){
    // apply new zoom
    m_renderService-> setZoom( zoomAmount );
}

void DataSource::setGamma( double gamma ){
    m_pixelPipeline->setGamma( gamma );
    m_renderService->setPixelPipeline( m_pixelPipeline, m_pixelPipeline->cacheId());
}


void DataSource::_updateClips( std::shared_ptr<NdArray::RawViewInterface>& view, int frameIndex,
        double minClipPercentile, double maxClipPercentile ){
    std::vector<double> clips = m_quantileCache[ frameIndex];
    NdArray::Double doubleView( view.get(), false );
    std::vector<double> newClips = Carta::Core::Algorithms::quantiles2pixels(
            doubleView, {minClipPercentile, maxClipPercentile });
    bool clipsChanged = false;
    if ( newClips.size() >= 2 ){
        if( clips.size() < 2 ){
            clipsChanged = true;
        }
        else {
            double ERROR_MARGIN = 0.000001;
            if ( qAbs( newClips[0] - clips[0]) > ERROR_MARGIN ||
                qAbs( newClips[1] - clips[1]) > ERROR_MARGIN ){
                clipsChanged = true;
            }
        }
    }
    if ( clipsChanged ){
        m_quantileCache[ frameIndex ] = newClips;
        m_pixelPipeline-> setMinMax( newClips[0], newClips[1] );
    }

}

void DataSource::_viewResize( const QSize& newSize ){
    m_renderService-> setOutputSize( newSize );
}


DataSource::~DataSource() {
    Carta::State::ObjectManager* objMan = Carta::State::ObjectManager::objectManager();
    if ( m_dataGrid != nullptr){
        objMan->removeObject(m_dataGrid->getId());
    }
}
}
}
