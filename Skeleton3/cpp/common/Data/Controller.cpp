#include "State/ObjectManager.h"

#include "Data/Controller.h"
#include "Data/DataSource.h"
#include "Data/Selection.h"
#include "Data/Animator.h"
#include "ImageView.h"

#include <QtCore/QDebug>
#include <QtCore/QList>
#include <memory>

using namespace std;

const QString Controller::CLIP_VALUE = "clipValue";
const QString Controller::AUTO_CLIP = "autoClip";
const QString Controller::DATA_COUNT = "dataCount";
const QString Controller::DATA_PATH = "dataPath";

const QString Controller::CLASS_NAME = "edu.nrao.carta.Controller";
bool Controller::m_registered =
    ObjectManager::objectManager()->registerClass (CLASS_NAME,
                                                   new Controller::Factory());

Controller::Controller( const QString& path, const QString& id ) :
        CartaObject( CLASS_NAME, path, id),
        m_selectChannel(nullptr),
        m_selectImage(nullptr),
        m_view( new ImageView( path, QColor("pink"), QImage())),
        m_state( path ){

    _initializeSelections();
    _initializeState();

     connect( m_selectChannel.get(), SIGNAL(indexChanged(bool)), this, SLOT(_loadView(bool)));
     connect( m_selectImage.get(), SIGNAL(indexChanged(bool)), this, SLOT(_loadView(bool)));

     _initializeCallbacks();

     registerView(m_view.get());

     //Load the view.
     _loadView( false );
}

void Controller::_initializeSelections(){
    _initializeSelection( m_selectChannel );
    _initializeSelection( m_selectImage );
}

void Controller::_initializeSelection( std::shared_ptr<Selection> & selection ){
    ObjectManager* objManager = ObjectManager::objectManager();
    QString selectId = objManager->createObject( Selection::CLASS_NAME );
    CartaObject* selectObj = objManager->getObject( selectId );
    selection.reset( dynamic_cast<Selection*>(selectObj) );
}

void Controller::_initializeCallbacks(){
    //Listen for updates to the clip and reload the frame.
    addStateCallback(CLIP_VALUE,
               [=] ( const QString & /*path*/, const QString & /*val*/) {
            if ( m_view ){
                   _loadView( true);
            }
    });

    /*m_state.addStateCallback( AUTO_CLIP,
            [=] ( const QString & path, const QString & val ) {
         string autoClipStr( AUTO_CLIP.toStdString());
         m_autoClip = m_state.getValue<bool>(autoClipStr, true );
         int dataCount = m_datas.size();
         for ( int i = 0; i < dataCount; i++ ){
             m_datas[i]->setAutoClip( autoClip );
         }
    });*/
}

void Controller::_initializeState(){
    //Set whether or not to auto clip
    m_state.insertValue<bool>( AUTO_CLIP, true );
    m_state.insertValue<double>( CLIP_VALUE, 0.95 );
    m_state.insertValue<int>(DATA_COUNT, 0 );
}

void Controller::addData(const QString& fileName) {
    //Find the location of the data, if it already exists.
    int targetIndex = -1;
    for (int i = 0; i < m_datas.size(); i++) {
        if (m_datas[i]->contains(fileName)) {
            targetIndex = i;
            break;
        }
    }

    //Add the data if it is not already there.
    if (targetIndex == -1) {
        //std::shared_ptr<DataSource> targetSource(new DataSource(fileName));
        ObjectManager* objManager = ObjectManager::objectManager();
        QString dataSourceId = objManager->createObject( DataSource::CLASS_NAME );
        CartaObject* dataSource = objManager->getObject( dataSourceId );
        std::shared_ptr<DataSource> targetSource( dynamic_cast<DataSource*>(dataSource));
        targetIndex = m_datas.size();
        m_datas.push_back(targetSource);

        //Update the data selectors upper bound and index based on the data.
        m_selectImage->setUpperBound(m_datas.size());

        saveState();
    }
    m_datas[targetIndex]->setFileName(fileName );
    int frameCount = m_datas[targetIndex]->getFrameCount();
    m_selectChannel->setUpperBound( frameCount );
    m_selectImage->setIndex(targetIndex);

    //Refresh the view of the data.
    _loadView( false );

    //Notify others there has been a change to the data.
    emit dataChanged();
}

void Controller::clear(){
    unregisterView();
}


void Controller::setFrameChannel(const QString& val) {
    if (m_selectChannel != nullptr) {
        m_selectChannel->setIndex(val);
    }
}

void Controller::setFrameImage(const QString& val) {
    if (m_selectImage != nullptr) {
        m_selectImage->setIndex(val);
    }
}

int Controller::getState( const QString& type, const QString& key ){

    int value = -1;
    if ( type == Animator::IMAGE ){
        value = m_selectImage->getState( key );
    }
    else if ( type == Animator::CHANNEL ){
        value = m_selectChannel->getState( key );
    }
    else {
        qDebug() << "DataController::getState unrecognized type="<<type;
    }
    return value;
}

void Controller::saveState() {
    //Note:: we need to save the number of data items that have been added
    //since otherwise, if data items have been deleted, their states will not
    //have been deleted, and we need to know when we read the states back in,
    //which ones represent valid data items and which ones do not.

    int dataCount = m_datas.size();
    m_state.setValue<int>( DATA_COUNT, dataCount );
    for (int i = 0; i < dataCount; i++) {
        m_datas[i]->saveState(/*m_winId, i*/);
    }
}



void Controller::_loadView( bool forceReload ) {

    //Determine the index of the data to load.
    int imageIndex = 0;
    if (m_selectImage != nullptr) {
        imageIndex = m_selectImage->getIndex();
    }

    if (imageIndex >= 0 && imageIndex < m_datas.size()) {
        if (m_datas[imageIndex] != nullptr) {

            //Determine the index of the channel to load.
            int frameIndex = 0;
            if (m_selectChannel != nullptr) {
                frameIndex = m_selectChannel->getIndex();
            }

            //Load the image.
            bool autoClip = m_state.getValue<bool>(AUTO_CLIP);
            double clipValue = m_state.getValue<double>(CLIP_VALUE);
            Nullable<QImage> res = m_datas[imageIndex]->load(frameIndex, forceReload, autoClip, clipValue);
            if (res.isNull()) {
                qDebug() << "Could not find any plugin to load image";
            } else {
                //Refresh the view.
                m_view->resetImage(res.val());
                refreshView( m_view.get());
            }
        }
    } else {
        qDebug() << "Image index=" << imageIndex << " is out of range";
    }
}



Controller::~Controller(){
    clear();
}