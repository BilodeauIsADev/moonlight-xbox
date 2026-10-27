#pragma once
namespace moonlight_xbox_dx {

    [Windows::UI::Xaml::Data::Bindable]
    public ref class MoonlightApp sealed : Windows::UI::Xaml::Data::INotifyPropertyChanged
    {
    private:
        Platform::String^ name;
        Platform::String^ imagePath = "ms-appx:///Assets/gamepad.svg";
        Platform::String^ logoPath;
        Platform::String^ backgroundPath;
        bool heroArtworkRequested = false;
        int id;
        bool currentlyRunning;
    public:
        //Thanks to https://phsucharee.wordpress.com/2013/06/19/data-binding-and-ccx-inotifypropertychanged/
        virtual event Windows::UI::Xaml::Data::PropertyChangedEventHandler^ PropertyChanged;

        void OnPropertyChanged(Platform::String^ propertyName);
        property Platform::String^ Name
        {
            Platform::String^ get() { return this->name; }
            void set(Platform::String^ value) {
                this->name = value;
                OnPropertyChanged("Name");
            }
        }

        property Platform::String^ ImagePath
        {
            Platform::String^ get() { 
                return imagePath;
            }
            void set(Platform::String^ path) {
                this->imagePath = path;
                OnPropertyChanged("ImagePath");
            }
        }
        
        property Platform::String^ LogoPath {
            Platform::String^ get() { return logoPath; }
            void set(Platform::String^ value) {
                logoPath = value;
                OnPropertyChanged("LogoPath");
            }
        }

        property Platform::String^ BackgroundPath {
            Platform::String^ get() { return backgroundPath; }
            void set(Platform::String^ value) {
                backgroundPath = value;
                OnPropertyChanged("BackgroundPath");
            }
        }

        property int Id
        {
            int get() { return this->id; }
            void set(int value) {
                this->id = value;
                OnPropertyChanged("Id");
            }
        }

        property bool CurrentlyRunning
        {
            bool get() { return this->currentlyRunning; }
            void set(bool value) {
                this->currentlyRunning = value;
                OnPropertyChanged("CurrentlyRunning");
            }
        }
    internal:
        // Called on the UI thread when the app is highlighted. Downloads once per
        // app-list entry, independently of the host and square-cover fetches.
        void EnsureHeroArtwork();
    };
}
